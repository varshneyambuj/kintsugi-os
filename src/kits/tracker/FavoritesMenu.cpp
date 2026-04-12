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
 * @file FavoritesMenu.cpp
 * @brief BSlowMenu subclasses that incrementally populate favorites and recents menus.
 *
 * FavoritesMenu builds its item list in stages: first the GoTo favorites folder,
 * then recent documents, then recent folders. RecentsMenu shows the most recently
 * used documents, applications, or folders for use in file panels and nav menus.
 *
 * @see BSlowMenu, BNavMenu, BRoster
 */


#include "FavoritesMenu.h"

#include <compat/sys/stat.h>

#include <Application.h>
#include <Catalog.h>
#include <FindDirectory.h>
#include <FilePanel.h>
#include <Locale.h>
#include <Message.h>
#include <Path.h>
#include <Query.h>
#include <Roster.h>

#include <functional>
#include <algorithm>

#include "IconMenuItem.h"
#include "PoseView.h"
#include "QueryPoseView.h"
#include "Tracker.h"
#include "Utilities.h"
#include "VirtualDirectoryEntryList.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "FavoritesMenu"


//	#pragma mark - FavoritesMenu


/**
 * @brief Construct a favorites/recents menu for use in a file panel.
 *
 * @param title              Menu title string.
 * @param openFolderMessage  Message template sent when a folder item is chosen.
 * @param openFileMessage    Message template sent when a file item is chosen.
 * @param target             BMessenger that receives open messages.
 * @param isSavePanel        true if the menu is in a Save panel (hides recent files).
 * @param filter             Optional BRefFilter applied to all entries.
 */
FavoritesMenu::FavoritesMenu(const char* title, BMessage* openFolderMessage,
	BMessage* openFileMessage, const BMessenger &target,
	bool isSavePanel, BRefFilter* filter)
	:
	BSlowMenu(title),
	fOpenFolderMessage(openFolderMessage),
	fOpenFileMessage(openFileMessage),
	fTarget(target),
	fState(kStart),
	fIndex(-1),
	fSectionItemCount(-1),
	fAddedSeparatorForSection(false),
	fContainer(NULL),
	fItemList(NULL),
	fInitialItemCount(0),
	fIsSavePanel(isSavePanel),
	fRefFilter(filter)
{
}


/**
 * @brief Destructor; deletes the owned message templates and entry container.
 */
FavoritesMenu::~FavoritesMenu()
{
	delete fOpenFolderMessage;
	delete fOpenFileMessage;
	delete fContainer;
}


/**
 * @brief Replace the BRefFilter used to decide which entries are shown.
 *
 * @param filter  New filter; may be NULL to show all entries.
 */
void
FavoritesMenu::SetRefFilter(BRefFilter* filter)
{
	fRefFilter = filter;
}


/**
 * @brief Prepare the menu for rebuilding by stripping stale dynamic items.
 *
 * @return true always to allow AddNextItem() to be called.
 */
bool
FavoritesMenu::StartBuildingItemList()
{
	// initialize the menu building state

	if (fInitialItemCount == 0)
		fInitialItemCount = CountItems();
	else {
		// strip the old items so we can add new fresh ones
		int32 count = CountItems() - fInitialItemCount;
		// keep the items that were added by the FavoritesMenu creator
		while (count--)
			delete RemoveItem(fInitialItemCount);
	}

	fUniqueRefCheck.clear();
	fState = kStart;
	return true;
}


/**
 * @brief Add the next item in the current build phase (favorites, files, or folders).
 *
 * Called repeatedly by BSlowMenu until it returns false, signalling completion.
 *
 * @return true if more items remain to be added; false when the list is complete.
 */
bool
FavoritesMenu::AddNextItem()
{
	// run the next chunk of code for a given item adding state

	if (fState == kStart) {
		fState = kAddingFavorites;
		fSectionItemCount = 0;
		fAddedSeparatorForSection = false;
		// set up adding the GoTo menu items

		try {
			BPath path;
			ThrowOnError(find_directory(B_USER_SETTINGS_DIRECTORY,
				&path, true));
			path.Append(kGoDirectory);
			mkdir(path.Path(), 0777);

			BEntry entry(path.Path());
			Model startModel(&entry, true);
			ThrowOnInitCheckError(&startModel);

			if (!startModel.IsContainer())
				throw B_ERROR;

			if (startModel.IsQuery())
				fContainer = new QueryEntryListCollection(&startModel);
			else if (startModel.IsVirtualDirectory())
				fContainer = new VirtualDirectoryEntryList(&startModel);
			else {
				BDirectory* directory
					= dynamic_cast<BDirectory*>(startModel.Node());
				if (directory != NULL)
					fContainer = new DirectoryEntryList(*directory);
			}

			ThrowOnInitCheckError(fContainer);
			ThrowOnError(fContainer->Rewind());
		} catch (...) {
			delete fContainer;
			fContainer = NULL;
		}
	}

	if (fState == kAddingFavorites) {
		entry_ref ref;
		if (fContainer != NULL && fContainer->GetNextRef(&ref) == B_OK) {
			Model model(&ref, true);
			if (model.InitCheck() != B_OK)
				return true;

			if (!ShouldShowModel(&model))
				return true;

			BMenuItem* item = BNavMenu::NewModelItem(&model,
				model.IsDirectory() ? fOpenFolderMessage : fOpenFileMessage,
				fTarget);

			if (item == NULL)
				return true;

			item->SetLabel(ref.name);
				// this is the name of the link in the Go dir

			if (!fAddedSeparatorForSection) {
				fAddedSeparatorForSection = true;
				AddItem(new TitledSeparatorItem(B_TRANSLATE("Favorites")));
			}
			fUniqueRefCheck.push_back(*model.EntryRef());
			AddItem(item);
			fSectionItemCount++;

			return true;
		}

		// done with favorites, set up for adding recent files
		fState = kAddingFiles;

		fAddedSeparatorForSection = false;

		app_info info;
		be_app->GetAppInfo(&info);
		fItems.MakeEmpty();

		int32 apps, docs, folders;
		TrackerSettings().RecentCounts(&apps, &docs, &folders);

		BRoster().GetRecentDocuments(&fItems, docs, NULL, info.signature);
		fIndex = 0;
		fSectionItemCount = 0;
	}

	if (fState == kAddingFiles) {
		//	if this is a Save panel, not an Open panel
		//	then don't add the recent documents
		if (!fIsSavePanel) {
			for (;;) {
				entry_ref ref;
				if (fItems.FindRef("refs", fIndex++, &ref) != B_OK)
					break;

				Model model(&ref, true);
				if (model.InitCheck() != B_OK)
					return true;

				if (!ShouldShowModel(&model))
					return true;

				BMenuItem* item = BNavMenu::NewModelItem(&model,
					fOpenFileMessage, fTarget);
				if (item) {
					if (!fAddedSeparatorForSection) {
						fAddedSeparatorForSection = true;
						AddItem(new TitledSeparatorItem(
							B_TRANSLATE("Recent documents")));
					}
					AddItem(item);
					fSectionItemCount++;
					return true;
				}
			}
		}

		// done with recent files, set up for adding recent folders
		fState = kAddingFolders;

		fAddedSeparatorForSection = false;

		app_info info;
		be_app->GetAppInfo(&info);
		fItems.MakeEmpty();

		int32 apps, docs, folders;
		TrackerSettings().RecentCounts(&apps, &docs, &folders);

		BRoster().GetRecentFolders(&fItems, folders, info.signature);
		fIndex = 0;
	}

	if (fState == kAddingFolders) {
		for (;;) {
			entry_ref ref;
			if (fItems.FindRef("refs", fIndex++, &ref) != B_OK)
				break;

			// don't add folders that are already in the GoTo section
			if (find_if(fUniqueRefCheck.begin(), fUniqueRefCheck.end(),
#if __GNUC__ <= 2
				bind2nd(std::equal_to<entry_ref>(), ref)
#else
				[ref](entry_ref compared) { return ref == compared; }
#endif
			)
					!= fUniqueRefCheck.end()) {
				continue;
			}

			Model model(&ref, true);
			if (model.InitCheck() != B_OK)
				return true;

			if (!ShouldShowModel(&model))
				return true;

			BMenuItem* item = BNavMenu::NewModelItem(&model,
				fOpenFolderMessage, fTarget, true);
			if (item != NULL) {
				if (!fAddedSeparatorForSection) {
					fAddedSeparatorForSection = true;
					AddItem(new TitledSeparatorItem(
						B_TRANSLATE("Recent folders")));
				}
				AddItem(item);
				item->SetEnabled(true);
					// BNavMenu::NewModelItem returns a disabled item here -
					// need to fix this in BNavMenu::NewModelItem

				return true;
			}
		}
	}

	return false;
}


/**
 * @brief Finalise the menu by setting the target for all added items.
 */
void
FavoritesMenu::DoneBuildingItemList()
{
	SetTargetForItems(fTarget);
}


/**
 * @brief Reset the build state so the menu is fully rebuilt on the next open.
 */
void
FavoritesMenu::ClearMenuBuildingState()
{
	delete fContainer;
	fContainer = NULL;
	fState = kDone;

	// force the menu to get rebuilt each time
	fMenuBuilt = false;
}


/**
 * @brief Decide whether @a model should appear in the menu.
 *
 * Applies the panel mode (save panels hide files) and the optional BRefFilter.
 *
 * @param model  The candidate directory or file model.
 * @return true if the model should be added to the menu.
 */
bool
FavoritesMenu::ShouldShowModel(const Model* model)
{
	if (fIsSavePanel && model->IsFile())
		return false;

	if (!fRefFilter || model->Node() == NULL)
		return true;

	struct stat_beos statBeOS;
	convert_to_stat_beos(model->StatBuf(), &statBeOS);

	return fRefFilter->Filter(model->EntryRef(), model->Node(), &statBeOS,
		model->MimeType());
}


//	#pragma mark - RecentsMenu


/**
 * @brief Construct a recents menu for documents, apps, or folders.
 *
 * @param name    Menu label.
 * @param which   0 = recent documents, 1 = applications, 2 = folders.
 * @param what    Message what-code sent when an item is selected.
 * @param target  Handler that receives selection messages.
 */
RecentsMenu::RecentsMenu(const char* name, int32 which, uint32 what,
	BHandler* target)
	:
	BNavMenu(name, what, target),
	fWhich(which),
	fRecentsCount(0),
	fItemIndex(0)
{
	int32 applications;
	int32 documents;
	int32 folders;
	TrackerSettings().RecentCounts(&applications,&documents,&folders);

	if (fWhich == 0)
		fRecentsCount = documents;
	else if (fWhich == 1)
		fRecentsCount = applications;
	else if (fWhich == 2)
		fRecentsCount = folders;
}


/**
 * @brief Detach without propagating to BNavMenu to avoid clearing the types list.
 */
void
RecentsMenu::DetachedFromWindow()
{
	//
	//	BNavMenu::DetachedFromWindow sets the TypesList to NULL
	//
	BMenu::DetachedFromWindow();
}


/**
 * @brief Clear all existing dynamic items to prepare for a fresh build.
 *
 * @return true always.
 */
bool
RecentsMenu::StartBuildingItemList()
{
	int32 count = CountItems()-1;
	for (int32 index = count; index >= 0; index--) {
		BMenuItem* item = ItemAt(index);
		ASSERT(item != NULL);

		RemoveItem(index);
		delete item;
	}
	//
	//	!! note: don't call inherited from here
	//	the navref is not set for this menu
	//	but it still needs to be a draggable navmenu
	//	simply return true so that AddNextItem is called
	//
	//	return BNavMenu::StartBuildingItemList();
	return true;
}


/**
 * @brief Add the next recent item; returns false when the list is exhausted.
 *
 * @return true if more items remain; false when done.
 */
bool
RecentsMenu::AddNextItem()
{
	if (fRecentsCount > 0 && AddRecents(fRecentsCount))
		return true;

	fItemIndex = 0;
	return false;
}


/**
 * @brief Populate fRecentList and add the next model item to the menu.
 *
 * Fetches the recent list on the first call (fItemIndex == 0) and then
 * iterates through it one entry per call.
 *
 * @param count  Maximum number of recent entries to fetch from the roster.
 * @return true if an item was added; false when the list is exhausted.
 */
bool
RecentsMenu::AddRecents(int32 count)
{
	if (fItemIndex == 0) {
		fRecentList.MakeEmpty();
		BRoster roster;

		switch(fWhich) {
			case 0:
				roster.GetRecentDocuments(&fRecentList, count);
				break;
			case 1:
				roster.GetRecentApps(&fRecentList, count);
				break;
			case 2:
				roster.GetRecentFolders(&fRecentList, count);
				break;
			default:
				return false;
				break;
		}
	}
	for (;;) {
		entry_ref ref;
		if (fRecentList.FindRef("refs", fItemIndex++, &ref) != B_OK)
			break;

		if (ref.name != NULL && strlen(ref.name) > 0) {
			Model model(&ref, true);
			ModelMenuItem* item = BNavMenu::NewModelItem(&model,
					new BMessage(fMessage.what),
					Target(), false, NULL, TypesList());

			if (item != NULL) {
				AddItem(item);

				//	return true so that we know to reenter this list
				return true;
			}

			return true;
		}
	}

	//
	//	return false if we are done with this list
	//
	return false;
}


/**
 * @brief Finalise the menu by adding a placeholder or setting item targets.
 */
void
RecentsMenu::DoneBuildingItemList()
{
	//
	//	!! note: don't call inherited here
	//	the object list is not built
	//	and this list does not need to be sorted
	//	BNavMenu::DoneBuildingItemList();
	//

	if (CountItems() <= 0) {
		BMenuItem* item = new BMenuItem(B_TRANSLATE("<No recent items>"), 0);
		item->SetEnabled(false);
		AddItem(item);
	} else
		SetTargetForItems(Target());
}


/**
 * @brief Force the menu to be rebuilt completely on the next open.
 */
void
RecentsMenu::ClearMenuBuildingState()
{
	fMenuBuilt = false;
	BNavMenu::ClearMenuBuildingState();
}
