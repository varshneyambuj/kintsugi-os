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
 *   Open Tracker License
 *
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of
 *   this software and associated documentation files (the "Software"), to deal in
 *   the Software without restriction, including without limitation the rights to
 *   use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *   of the Software, and to permit persons to whom the Software is furnished to do
 *   so, subject to the following conditions:
 *
 *   The above copyright notice and this permission notice applies to all licensees
 *   and shall be included in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
 *   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 *   BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 *   AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
 *   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *   Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered
 *   trademarks of Be Incorporated in the United States and other countries.
 *   All rights reserved.
 */


/**
 * @file IconMenuItem.cpp
 * @brief Menu item classes that draw small file-system icons alongside their labels.
 *
 * Provides ModelMenuItem (draws the icon of a filesystem Model object),
 * SpecialModelMenuItem (italic label variant), and IconMenuItem (draws an
 * arbitrary BBitmap icon). These are used throughout the Tracker menus for
 * mount menus, the "Copy To" list, new-file templates, and nav-menu items.
 *
 * @see BMenuItem, IconCache, Model
 */

//! Menu items with small icons.


#include "IconMenuItem.h"

#include <ControlLook.h>
#include <Debug.h>
#include <Menu.h>
#include <MenuField.h>
#include <MenuPrivate.h>
#include <NodeInfo.h>

#include "IconCache.h"


/**
 * @brief IconCache draw callback that renders a bitmap at half opacity.
 *
 * Used to indicate disabled menu items: draws with B_CONSTANT_ALPHA blending
 * (B_RGBA32) or B_OP_BLEND (palette) depending on the bitmap colour space.
 *
 * @param view    The BView to draw into.
 * @param where   Top-left position for the bitmap.
 * @param bitmap  The source bitmap to render.
 */
static void
DimmedIconBlitter(BView* view, BPoint where, BBitmap* bitmap, void*)
{
	if (bitmap->ColorSpace() == B_RGBA32) {
		rgb_color oldHighColor = view->HighColor();
		view->SetHighColor(0, 0, 0, 128);
		view->SetDrawingMode(B_OP_ALPHA);
		view->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
		view->DrawBitmap(bitmap, where);
		view->SetHighColor(oldHighColor);
	} else {
		view->SetDrawingMode(B_OP_BLEND);
		view->DrawBitmap(bitmap, where);
	}
	view->SetDrawingMode(B_OP_OVER);
}


//	#pragma mark - ModelMenuItem


/**
 * @brief Construct a ModelMenuItem with an explicit label.
 *
 * @param model      The filesystem model whose icon is drawn.
 * @param title      Label string for the item.
 * @param message    Invocation message (ownership taken).
 * @param shortcut   Optional keyboard shortcut character.
 * @param modifiers  Optional modifier mask for the shortcut.
 * @param drawText   If false, only the icon is drawn (no label).
 * @param extraPad   If true, adds extra horizontal padding for use in a BMenuBar.
 */
ModelMenuItem::ModelMenuItem(const Model* model, const char* title,
	BMessage* message, char shortcut, uint32 modifiers,
	bool drawText, bool extraPad)
	:
	BMenuItem(title, message, shortcut, modifiers),
	fModel(*model),
	fHeightDelta(0),
	fDrawText(drawText),
	fExtraPad(extraPad)
{
	ThrowOnInitCheckError(&fModel);
	// The 'fExtraPad' field is used to when this menu item is added to
	// a menubar instead of a menu. Menus and MenuBars space out items
	// differently (more space around items in a menu). This class wants
	// to be able to space item the same, no matter where they are. The
	// fExtraPad field allows for that.

	if (model->IsRoot())
		SetLabel(model->Name());

	// ModelMenuItem is used in synchronously invoked menus, make sure
	// we invoke with a timeout
	SetTimeout(kSynchMenuInvokeTimeout);
}


/**
 * @brief Construct a ModelMenuItem that hosts a submenu.
 *
 * @param model     The filesystem model whose icon is drawn.
 * @param menu      The submenu to attach to this item.
 * @param drawText  If false, only the icon is drawn.
 * @param extraPad  If true, adds extra horizontal padding for use in a BMenuBar.
 */
ModelMenuItem::ModelMenuItem(const Model* model, BMenu* menu, bool drawText,
	bool extraPad)
	:
	BMenuItem(menu),
	fModel(*model),
	fHeightDelta(0),
	fDrawText(drawText),
	fExtraPad(extraPad)
{
	ThrowOnInitCheckError(&fModel);
	// ModelMenuItem is used in synchronously invoked menus, make sure
	// we invoke with a timeout
	SetTimeout(kSynchMenuInvokeTimeout);
}


/**
 * @brief Destructor.
 */
ModelMenuItem::~ModelMenuItem()
{
}


/**
 * @brief Re-point this item's model to a different filesystem entry.
 *
 * @param entry  The new BEntry to set the internal model to.
 * @return B_OK on success, or an error code.
 */
status_t
ModelMenuItem::SetEntry(const BEntry* entry)
{
	return fModel.SetTo(entry);
}


/**
 * @brief Draw the item label (if enabled) and the model icon.
 */
void
ModelMenuItem::DrawContent()
{
	if (fDrawText) {
		BPoint drawPoint(ContentLocation());
		drawPoint.x += ListIconSize() + ListIconSize() / 4 + _ExtraLeftPadding();
		if (fHeightDelta > 0)
			drawPoint.y += ceilf(fHeightDelta / 2);

		Menu()->MovePenTo(drawPoint);
		_inherited::DrawContent();
	}
	DrawIcon();
}


/**
 * @brief Toggle the highlight state and redraw the icon.
 *
 * @param hilited  true to highlight; false to unhighlight.
 */
void
ModelMenuItem::Highlight(bool hilited)
{
	_inherited::Highlight(hilited);
	DrawIcon();
}


/**
 * @brief Draw the model icon using the icon cache, using a dim blitter if disabled.
 */
void
ModelMenuItem::DrawIcon()
{
	Menu()->PushState();

	BPoint where(ContentLocation());
	// center icon with text.

	float deltaHeight = fHeightDelta < 0 ? -fHeightDelta : 0;
	where.y += ceilf(deltaHeight / 2);

	where.x += _ExtraLeftPadding();

	Menu()->SetDrawingMode(B_OP_OVER);
	Menu()->SetLowColor(B_TRANSPARENT_32_BIT);

	// draw small icon, synchronously
	if (IsEnabled()) {
		IconCache::sIconCache->Draw(fModel.ResolveIfLink(), Menu(), where,
			kNormalIcon, BSize(ListIconSize() - 1, ListIconSize() - 1));
	} else {
		// dimmed, for now use a special blitter; icon cache should
		// know how to blit one eventually
		IconCache::sIconCache->SyncDraw(fModel.ResolveIfLink(), Menu(), where,
			kNormalIcon, BSize(ListIconSize() - 1, ListIconSize() - 1), DimmedIconBlitter);
	}

	Menu()->PopState();
}


/**
 * @brief Compute the extra left padding needed to align with a BMenu item when in a BMenuBar.
 *
 * @return Left-margin delta in pixels, or 0 if fExtraPad is false.
 */
float
ModelMenuItem::_ExtraLeftPadding()
{
	if (!fExtraPad)
		return 0;

	// BMenu and BMenuBar have different margins,
	// we want to make them the same. See fExtraPad.
	float leftDelta;
	_GetHorizontalItemMarginDelta(&leftDelta, NULL);

	return leftDelta;
}


/**
 * @brief Compute the total extra horizontal padding (left + right) for BMenuBar alignment.
 *
 * @return Total padding delta in pixels, or 0 if fExtraPad is false.
 */
float
ModelMenuItem::_ExtraPadding()
{
	if (!fExtraPad)
		return 0;

	// BMenu and BMenuBar have different margins,
	// we want to make them the same. See fExtraPad.
	float leftDelta, rightDelta;
	_GetHorizontalItemMarginDelta(&leftDelta, &rightDelta);

	return leftDelta + rightDelta;
}


/**
 * @brief Measure the left and right margin differences between BMenu and BMenuBar items.
 *
 * @param _leftDelta   Receives the left-margin difference; may be NULL.
 * @param _rightDelta  Receives the right-margin difference; may be NULL.
 */
void
ModelMenuItem::_GetHorizontalItemMarginDelta(float* _leftDelta, float* _rightDelta)
{
	float menuLeft, menuRight, menuBarLeft, menuBarRight;

	BMenu tempMenu("temp");
	BPrivate::MenuPrivate menuPrivate(&tempMenu);
	menuPrivate.GetItemMargins(&menuLeft, NULL, &menuRight, NULL);

	BPrivate::MenuPrivate menuBarPrivate(Menu());
	menuBarPrivate.GetItemMargins(&menuBarLeft, NULL, &menuBarRight, NULL);

	if (_leftDelta != NULL)
		*_leftDelta = menuLeft - menuBarLeft;

	if (_rightDelta != NULL)
		*_rightDelta = menuRight - menuBarRight;
}


/**
 * @brief Report the content size needed to accommodate both label and icon.
 *
 * @param width   Receives the minimum content width.
 * @param height  Receives the minimum content height (at least icon height).
 */
void
ModelMenuItem::GetContentSize(float* width, float* height)
{
	_inherited::GetContentSize(width, height);

	const float iconSize = ListIconSize();
	fHeightDelta = iconSize - *height;
	if (*height < iconSize)
		*height = iconSize;

	*width += iconSize + iconSize / 4 + _ExtraPadding();
}


/**
 * @brief Invoke the item, stripping node-close refs unless the Option key is held.
 *
 * @param message  Message to invoke; uses the item's own message if NULL.
 * @return B_OK on success, or an error code.
 */
status_t
ModelMenuItem::Invoke(BMessage* message)
{
	if (Menu() == NULL)
		return B_ERROR;

	if (!IsEnabled())
		return B_ERROR;

	if (message == NULL)
		message = Message();

	if (message == NULL)
		return B_BAD_VALUE;

	BMessage clone(*message);
	clone.AddInt32("index", Menu()->IndexOf(this));
	clone.AddInt64("when", system_time());
	clone.AddPointer("source", this);

	if ((modifiers() & B_OPTION_KEY) == 0) {
		// if option not held, remove refs to close to prevent closing
		// parent window
		clone.RemoveData("nodeRefsToClose");
	}

	return BInvoker::Invoke(&clone);
}


//	#pragma mark - SpecialModelMenuItem


/*!	A ModelMenuItem subclass that draws its label in italics.

	It's used for example in the "Copy To" menu to indicate some special
	folders like the parent folder.
*/
SpecialModelMenuItem::SpecialModelMenuItem(const Model* model, BMenu* menu)
	: ModelMenuItem(model, menu)
{
}


void
SpecialModelMenuItem::DrawContent()
{
	Menu()->PushState();

	BFont font;
	Menu()->GetFont(&font);
	font.SetFace(B_ITALIC_FACE);
	Menu()->SetFont(&font);

	_inherited::DrawContent();
	Menu()->PopState();
}


//	#pragma mark - IconMenuItem


/*!	A menu item that draws an icon alongside the label.

	It's currently used in the mount and new file template menus.
*/
/**
 * @brief Construct an icon menu item from a pre-decoded BBitmap.
 *
 * @param label    Text label.
 * @param message  Invocation message (ownership taken).
 * @param icon     Source bitmap; a scaled copy is stored internally.
 * @param which    Desired icon size.
 */
IconMenuItem::IconMenuItem(const char* label, BMessage* message, BBitmap* icon,
	icon_size which)
	:
	PositionPassingMenuItem(label, message),
	fDeviceIcon(NULL),
	fHeightDelta(0),
	fWhich(which)
{
	SetIcon(icon);

	// IconMenuItem is used in synchronously invoked menus, make sure
	// we invoke with a timeout
	SetTimeout(kSynchMenuInvokeTimeout);
}


/**
 * @brief Construct an icon menu item loading the icon from a BNodeInfo.
 *
 * @param label     Text label.
 * @param message   Invocation message.
 * @param nodeInfo  Node info from which the tracker icon is retrieved; may be NULL.
 * @param which     Desired icon size.
 */
IconMenuItem::IconMenuItem(const char* label, BMessage* message,
	const BNodeInfo* nodeInfo, icon_size which)
	:
	PositionPassingMenuItem(label, message),
	fDeviceIcon(NULL),
	fHeightDelta(0),
	fWhich(which)
{
	if (nodeInfo != NULL) {
		fDeviceIcon = new BBitmap(BRect(BPoint(0, 0),
			be_control_look->ComposeIconSize(which)), kDefaultIconDepth);
		if (nodeInfo->GetTrackerIcon(fDeviceIcon, (icon_size)-1) != B_OK) {
			delete fDeviceIcon;
			fDeviceIcon = NULL;
		}
	}

	// IconMenuItem is used in synchronously invoked menus, make sure
	// we invoke with a timeout
	SetTimeout(kSynchMenuInvokeTimeout);
}


/**
 * @brief Construct an icon menu item loading the icon from a MIME type string.
 *
 * Falls back to the supertype's icon if the specific type has none.
 *
 * @param label     Text label.
 * @param message   Invocation message.
 * @param iconType  MIME type string (e.g. "text/plain").
 * @param which     Desired icon size.
 */
IconMenuItem::IconMenuItem(const char* label, BMessage* message,
	const char* iconType, icon_size which)
	:
	PositionPassingMenuItem(label, message),
	fDeviceIcon(NULL),
	fHeightDelta(0),
	fWhich(which)
{
	BMimeType mime(iconType);
	fDeviceIcon = new BBitmap(BRect(BPoint(0, 0), be_control_look->ComposeIconSize(which)),
		kDefaultIconDepth);

	if (mime.GetIcon(fDeviceIcon, which) != B_OK) {
		BMimeType super;
		mime.GetSupertype(&super);
		if (super.GetIcon(fDeviceIcon, which) != B_OK) {
			delete fDeviceIcon;
			fDeviceIcon = NULL;
		}
	}

	// IconMenuItem is used in synchronously invoked menus, make sure
	// we invoke with a timeout
	SetTimeout(kSynchMenuInvokeTimeout);
}


/**
 * @brief Construct an icon menu item with a submenu, loading the icon from a MIME type.
 *
 * @param submenu   The submenu to attach to this item.
 * @param message   Invocation message.
 * @param iconType  MIME type string.
 * @param which     Desired icon size.
 */
IconMenuItem::IconMenuItem(BMenu* submenu, BMessage* message,
	const char* iconType, icon_size which)
	:
	PositionPassingMenuItem(submenu, message),
	fDeviceIcon(NULL),
	fHeightDelta(0),
	fWhich(which)
{
	BMimeType mime(iconType);
	fDeviceIcon = new BBitmap(BRect(BPoint(0, 0), be_control_look->ComposeIconSize(which)),
		kDefaultIconDepth);

	if (mime.GetIcon(fDeviceIcon, which) != B_OK) {
		BMimeType super;
		mime.GetSupertype(&super);
		if (super.GetIcon(fDeviceIcon, which) != B_OK) {
			delete fDeviceIcon;
			fDeviceIcon = NULL;
		}
	}

	// IconMenuItem is used in synchronously invoked menus, make sure
	// we invoke with a timeout
	SetTimeout(kSynchMenuInvokeTimeout);
}


/**
 * @brief Unarchive constructor used by BArchivable::Instantiate().
 *
 * @param data  BMessage archive containing "_which" and optional "_deviceIconBits".
 */
IconMenuItem::IconMenuItem(BMessage* data)
	:
	PositionPassingMenuItem(data),
	fDeviceIcon(NULL),
	fHeightDelta(0),
	fWhich(B_MINI_ICON)
{
	if (data != NULL) {
		fWhich = (icon_size)data->GetInt32("_which", B_MINI_ICON);

		fDeviceIcon = new BBitmap(BRect(BPoint(0, 0), be_control_look->ComposeIconSize(fWhich)),
			kDefaultIconDepth);

		if (data->HasData("_deviceIconBits", B_RAW_TYPE)) {
			ssize_t numBytes;
			const void* bits;
			if (data->FindData("_deviceIconBits", B_RAW_TYPE, &bits, &numBytes)
					== B_OK) {
				fDeviceIcon->SetBits(bits, numBytes, (int32)0,
					kDefaultIconDepth);
			}
		}
	}

	// IconMenuItem is used in synchronously invoked menus, make sure
	// we invoke with a timeout
	SetTimeout(kSynchMenuInvokeTimeout);
}


/**
 * @brief Create an IconMenuItem from an archive message (BArchivable factory).
 *
 * @param data  The archive BMessage.
 * @return A new IconMenuItem, or NULL if instantiation fails.
 */
BArchivable*
IconMenuItem::Instantiate(BMessage* data)
{
	//if (validate_instantiation(data, "IconMenuItem"))
		return new IconMenuItem(data);

	return NULL;
}


/**
 * @brief Archive this item into @a data, including icon size and pixel data.
 *
 * @param data  Output BMessage to archive into.
 * @param deep  Whether to recursively archive children (passed to base class).
 * @return B_OK on success, or an error code.
 */
status_t
IconMenuItem::Archive(BMessage* data, bool deep) const
{
	status_t result = PositionPassingMenuItem::Archive(data, deep);

	if (result == B_OK)
		result = data->AddInt32("_which", (int32)fWhich);

	if (result == B_OK && fDeviceIcon != NULL) {
		result = data->AddData("_deviceIconBits", B_RAW_TYPE,
			fDeviceIcon->Bits(), fDeviceIcon->BitsLength());
	}

	return result;
}


/**
 * @brief Destructor; deletes the owned icon bitmap.
 */
IconMenuItem::~IconMenuItem()
{
	delete fDeviceIcon;
}


/**
 * @brief Report the content size needed to accommodate the icon and label.
 *
 * @param width   Receives the content width.
 * @param height  Receives the content height (at least icon height).
 */
void
IconMenuItem::GetContentSize(float* width, float* height)
{
	_inherited::GetContentSize(width, height);

	int32 iconHeight = fWhich;
	if (fDeviceIcon != NULL)
		iconHeight = fDeviceIcon->Bounds().IntegerHeight() + 1;

	fHeightDelta = iconHeight - *height;
	if (*height < iconHeight)
		*height = iconHeight;

	if (fDeviceIcon != NULL)
		*width += fDeviceIcon->Bounds().Width() + be_control_look->DefaultLabelSpacing();
}


/**
 * @brief Draw the label (offset past the icon) and then the icon bitmap.
 */
void
IconMenuItem::DrawContent()
{
	BPoint drawPoint(ContentLocation());
	if (fDeviceIcon != NULL)
		drawPoint.x += fDeviceIcon->Bounds().Width() + be_control_look->DefaultLabelSpacing();

	if (fHeightDelta > 0)
		drawPoint.y += ceilf(fHeightDelta / 2);

	Menu()->MovePenTo(drawPoint);
	_inherited::DrawContent();

	Menu()->PushState();

	BPoint where(ContentLocation());
	float deltaHeight = fHeightDelta < 0 ? -fHeightDelta : 0;
	where.y += ceilf(deltaHeight / 2);

	if (fDeviceIcon != NULL) {
		if (IsEnabled())
			Menu()->SetDrawingMode(B_OP_ALPHA);
		else {
			Menu()->SetDrawingMode(B_OP_ALPHA);
			Menu()->SetHighColor(0, 0, 0, 64);
			Menu()->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
		}
		Menu()->DrawBitmapAsync(fDeviceIcon, where);
	}

	Menu()->PopState();
}


/**
 * @brief Mark this item and propagate the icon to the topmost BMenuField parent.
 *
 * When an IconMenuItem inside a BMenuField's menu is marked, the top-level
 * menu item's icon is updated to match so the field shows the selected icon.
 *
 * @param mark  true to mark; false to unmark.
 */
void
IconMenuItem::SetMarked(bool mark)
{
	_inherited::SetMarked(mark);

	if (!mark)
		return;

	// we are marking the item

	BMenu* menu = Menu();
	if (menu == NULL)
		return;

	// we have a parent menu

	BMenu* _menu = menu;
	while ((_menu = _menu->Supermenu()) != NULL)
		menu = _menu;

	// went up the hierarchy to found the topmost menu

	if (menu == NULL || menu->Parent() == NULL)
		return;

	// our topmost menu has a parent

	if (dynamic_cast<BMenuField*>(menu->Parent()) == NULL)
		return;

	// our topmost menu's parent is a BMenuField

	BMenuItem* topLevelItem = menu->ItemAt((int32)0);

	if (topLevelItem == NULL)
		return;

	// our topmost menu has a menu item

	IconMenuItem* topLevelMenuItem = dynamic_cast<IconMenuItem*>(topLevelItem);
	if (topLevelMenuItem == NULL)
		return;

	// our topmost menu's item is an IconMenuItem

	// update the icon
	topLevelMenuItem->SetIcon(fDeviceIcon);
	menu->Invalidate();
}


/**
 * @brief Replace the displayed icon with a copy of @a icon.
 *
 * Deletes any existing icon and imports @a icon into a new BBitmap scaled to
 * fWhich. If @a icon is NULL, the icon is cleared.
 *
 * @param icon  Source bitmap; ownership is NOT taken.
 */
void
IconMenuItem::SetIcon(BBitmap* icon)
{
	if (icon != NULL) {
		if (fDeviceIcon != NULL)
			delete fDeviceIcon;

		fDeviceIcon = new BBitmap(BRect(BPoint(0, 0),
			be_control_look->ComposeIconSize(fWhich)), icon->ColorSpace());
		fDeviceIcon->ImportBits(icon);
	} else {
		delete fDeviceIcon;
		fDeviceIcon = NULL;
	}
}
