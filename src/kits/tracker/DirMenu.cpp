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
 * @file DirMenu.cpp
 * @brief Pop-up menu that walks the parent-directory hierarchy of a Tracker window.
 *
 * BDirMenu populates a BPopUpMenu with ModelMenuItems for each ancestor directory
 * from the current folder up to the Desktop (or root), allowing the user to jump
 * to any level in the path. Optionally includes a Disks icon and nav-menu submenus.
 *
 * @see BPopUpMenu, ModelMenuItem, BNavMenu
 */

// ToDo:
// get rid of fMenuBar, SetMenuBar and related mess


#include <Catalog.h>
#include <Debug.h>
#include <Directory.h>
#include <Locale.h>
#include <MenuBar.h>
#include <Path.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include "Attributes.h"
#include "ContainerWindow.h"
#include "DirMenu.h"
#include "FSUtils.h"
#include "IconMenuItem.h"
#include "NavMenu.h"
#include "TrackerSettings.h"
#include "Utilities.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "DirMenu"


//	#pragma mark - BDirMenu


/**
 * @brief Construct an empty directory hierarchy menu.
 *
 * @param bar        Optional BMenuBar that receives a top-level navigation item.
 * @param target     BMessenger that receives open-directory messages.
 * @param command    Message what-code sent when an item is selected.
 * @param entryName  Name of the "refs" field in the sent message; defaults to "refs".
 */
BDirMenu::BDirMenu(BMenuBar* bar, BMessenger target, uint32 command,
	const char* entryName)
	:
	BPopUpMenu("directories"),
	fTarget(target),
	fMenuBar(bar),
	fCommand(command)
{
	if (entryName)
		fEntryName = entryName;
	else
		fEntryName = "refs";
}


/**
 * @brief Destructor.
 */
BDirMenu::~BDirMenu()
{
}


/**
 * @brief Walk the directory hierarchy from @a startEntry and add items to the menu.
 *
 * Traverses parent directories up to the Desktop or root, respecting the
 * Desktop-file-panel-root and ShowDisksIcon settings. Invisible directories are
 * skipped. Optionally reverses item order, adds keyboard shortcuts, and wraps
 * each item in a BNavMenu submenu.
 *
 * @param startEntry       The starting directory entry.
 * @param source           The window providing context (used to close it on navigation).
 * @param includeStartEntry  If true, the starting entry itself is added as first item.
 * @param select           If true, the bottommost item is marked as the current selection.
 * @param reverse          If true, items are prepended so the topmost directory is last.
 * @param addShortcuts     If true, desktop and home directory items get keyboard shortcuts.
 * @param navMenuEntries   If true, each item gets a BNavMenu submenu for browsing.
 */
void
BDirMenu::Populate(const BEntry* startEntry, BWindow* source,
	bool includeStartEntry, bool select, bool reverse, bool addShortcuts,
	bool navMenuEntries)
{
	try {
		if (!startEntry)
			throw (status_t)B_ERROR;

		Model model(startEntry);
		ThrowOnInitCheckError(&model);

		ModelMenuItem* menu = NULL;

		if (fMenuBar != NULL) {
			menu = new ModelMenuItem(&model, this, true, true);
			fMenuBar->AddItem(menu);
		}

		BEntry entry(*startEntry);

		bool showDesktop, showDisksIcon;
		{
			TrackerSettings settings;
			showDesktop = settings.DesktopFilePanelRoot();
			showDisksIcon = settings.ShowDisksIcon();
		}

		// might start one level above startEntry
		if (!includeStartEntry) {
			BDirectory parent;
			BDirectory dir(&entry);

			if (!showDesktop && dir.InitCheck() == B_OK && dir.IsRootDirectory()) {
				// if we're at the root directory skip "mnt" and
				// go straight to "/"
				parent.SetTo("/");
				parent.GetEntry(&entry);
			} else
				FSGetParentVirtualDirectoryAware(entry, entry);
		}

		BDirectory desktopDir;
		FSGetDeskDir(&desktopDir);
		BEntry desktopEntry;
		desktopDir.GetEntry(&desktopEntry);

		for (;;) {
			BNode node(&entry);
			ThrowOnInitCheckError(&node);

			PoseInfo info;
			ReadAttrResult result = ReadAttr(&node, kAttrPoseInfo,
				kAttrPoseInfoForeign, B_RAW_TYPE, 0, &info, sizeof(PoseInfo),
				&PoseInfo::EndianSwap);

			BEntry parentEntry;
			bool hitRoot = false;

			BDirectory dir(&entry);
			if (!showDesktop && dir.InitCheck() == B_OK && dir.IsRootDirectory()) {
				// if we're at the root directory skip "mnt" and
				// go straight to "/"
				hitRoot = true;
				parentEntry.SetTo("/");
			} else
				FSGetParentVirtualDirectoryAware(entry, parentEntry);

			if (showDesktop) {
				BEntry root("/");
				// warp from "/" to Desktop properly
				if (entry == root) {
					if (showDisksIcon)
						AddDisksIconToMenu(reverse);
					entry = desktopEntry;
				}

				if (entry == desktopEntry)
					hitRoot = true;
			}

			if (result == kReadAttrFailed || !info.fInvisible
				|| (showDesktop && desktopEntry == entry)) {
				AddItemToDirMenu(&entry, source, reverse, addShortcuts, navMenuEntries);
			}

			if (hitRoot) {
				if (!showDesktop && showDisksIcon && *startEntry != "/")
					AddDisksIconToMenu(reverse);
				break;
			}

			entry = parentEntry;
			if (entry.InitCheck() != B_OK)
				break;
		}

		// select last item in menu
		if (!select)
			return;

		ModelMenuItem* item = dynamic_cast<ModelMenuItem*>(ItemAt(CountItems() - 1));
		if (item != NULL) {
			item->SetMarked(true);
			if (menu) {
				entry.SetTo(item->TargetModel()->EntryRef());
				ThrowOnError(menu->SetEntry(&entry));
			}
		}
	} catch (status_t err) {
		PRINT(("BDirMenu::Populate: caught error %s\n", strerror(err)));
		if (!CountItems()) {
			BString error;
			error << "Error [" << strerror(err) << "] populating menu";
			AddItem(new BMenuItem(error.String(), 0));
		}
	}
}


/**
 * @brief Create a ModelMenuItem for @a entry and insert it into the menu.
 *
 * Optionally wraps the item in a BNavMenu and assigns desktop/home shortcuts.
 *
 * @param entry          The directory entry to add.
 * @param source         The container window for node-close context data.
 * @param atEnd          If true, appends the item; otherwise prepends it.
 * @param addShortcuts   If true, assigns D or H shortcuts to special directories.
 * @param navMenuEntries If true, attaches a BNavMenu submenu to the item.
 */
void
BDirMenu::AddItemToDirMenu(const BEntry* entry, BWindow* source,
	bool atEnd, bool addShortcuts, bool navMenuEntries)
{
	Model model(entry);
	if (model.InitCheck() != B_OK)
		return;

	BMessage* message = new BMessage(fCommand);
	message->AddRef(fEntryName.String(), model.EntryRef());

	// add reference to the container windows model so that we can
	// close the window if
	BContainerWindow* window = dynamic_cast<BContainerWindow*>(source);
	if (window != NULL) {
		message->AddData("nodeRefsToClose", B_RAW_TYPE,
			window->TargetModel()->NodeRef(), sizeof(node_ref));
	}
	ModelMenuItem* item;
	if (navMenuEntries) {
		BNavMenu* subMenu = new BNavMenu(model.Name(), fCommand, fTarget, source);
		entry_ref ref;
		entry->GetRef(&ref);
		subMenu->SetNavDir(&ref);
		item = new ModelMenuItem(&model, subMenu);
		item->SetLabel(model.Name());
		item->SetMessage(message);
	} else {
		item = new ModelMenuItem(&model, model.Name(), message);
	}

	if (addShortcuts) {
		if (model.IsDesktop())
			item->SetShortcut('D', B_COMMAND_KEY);
		else if (FSIsHomeDir(entry))
			item->SetShortcut('H', B_COMMAND_KEY);
	}

	if (atEnd)
		AddItem(item);
	else
		AddItem(item, 0);

	item->SetTarget(fTarget);

	if (fMenuBar != NULL) {
		ModelMenuItem* menu = dynamic_cast<ModelMenuItem*>(fMenuBar->ItemAt(0));
		if (menu != NULL) {
			ThrowOnError(menu->SetEntry(entry));
			item->SetMarked(true);
		}
	}
}


/**
 * @brief Insert the root (Disks) icon item into the menu.
 *
 * Creates a ModelMenuItem for "/" with a BNavMenu submenu.
 *
 * @param atEnd  If true, appends the item; otherwise prepends it.
 */
void
BDirMenu::AddDisksIconToMenu(bool atEnd)
{
	BEntry entry("/");
	Model model(&entry);
	if (model.InitCheck() != B_OK)
		return;

	entry_ref ref;
	entry.GetRef(&ref);
	BMessage* message = new BMessage(fCommand);
	message->AddRef(fEntryName.String(), &ref);

	BNavMenu* subMenu = new BNavMenu(model.Name(), fCommand, fTarget);
	subMenu->SetNavDir(&ref);
	ModelMenuItem* item = new ModelMenuItem(&model, subMenu);
	item->SetLabel(model.Name());
	item->SetMessage(message);

	if (atEnd)
		AddItem(item);
	else
		AddItem(item, 0);

	item->SetTarget(fTarget);
}
