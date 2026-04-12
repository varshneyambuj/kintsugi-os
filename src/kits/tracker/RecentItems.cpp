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
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *   Distributed under the terms of the OpenTracker License.
 */


/**
 * @file RecentItems.cpp
 * @brief Lazily-built menus for recently-used files, folders, and applications.
 *
 * RecentItemsMenu is a BSlowMenu subclass that calls BRecentItemsList to
 * retrieve recent entries and adds icon menu items one at a time so the menu
 * remains interruptible.  Subclasses RecentFilesMenu, RecentFoldersMenu, and
 * RecentAppsMenu specialise the list type and open message.
 *
 * @see BSlowMenu, BRecentItemsList, BNavMenu
 */


#include "RecentItems.h"

#include <Roster.h>

#include "Attributes.h"
#include "IconMenuItem.h"
#include "Model.h"
#include "NavMenu.h"
#include "PoseView.h"
#include "SlowMenu.h"
#include "Tracker.h"
#include "Utilities.h"


class RecentItemsMenu : public BSlowMenu {
public:
	RecentItemsMenu(const char* title, BMessage* openMessage,
		BHandler* itemTarget, int32 maxItems);
	virtual ~RecentItemsMenu();

	virtual bool StartBuildingItemList();
	virtual bool AddNextItem();
	virtual void DoneBuildingItemList() {}
	virtual void ClearMenuBuildingState();

protected:
	virtual const BMessage* FileMessage()
	{
		return fTargetMesage;
	}

	virtual const BMessage* ContainerMessage()
	{
		return fTargetMesage;
	}

	BRecentItemsList* fIterator;
	BMessage* fTargetMesage;
	BHandler* fItemTarget;
	int32 fCount;
	int32 fSanityCount;
	int32 fMaxCount;
};


class RecentFilesMenu : public RecentItemsMenu {
public:
	RecentFilesMenu(const char* title, BMessage* openFileMessage,
		BMessage* openFolderMessage, BHandler* target,
		int32 maxItems, bool navMenuFolders, const char* ofType,
		const char* openedByAppSig);

	RecentFilesMenu(const char* title, BMessage* openFileMessage,
		BMessage* openFolderMessage, BHandler* target,
		int32 maxItems, bool navMenuFolders, const char* ofTypeList[],
		int32 ofTypeListCount, const char* openedByAppSig);

	virtual ~RecentFilesMenu();

protected:
	virtual const BMessage* ContainerMessage()
		{ return openFolderMessage; }

private:
	BMessage* openFolderMessage;
};


class RecentFoldersMenu : public RecentItemsMenu {
public:
	RecentFoldersMenu(const char* title, BMessage* openMessage,
		BHandler* target, int32 maxItems, bool navMenuFolders,
		const char* openedByAppSig);
};


class RecentAppsMenu : public RecentItemsMenu {
public:
	RecentAppsMenu(const char* title, BMessage* openMessage,
		BHandler* target, int32 maxItems);
};


// #pragma mark - RecentItemsMenu


/**
 * @brief Construct a RecentItemsMenu.
 *
 * @param title       The menu title string.
 * @param openMessage The BMessage prototype sent when an item is activated.
 * @param itemTarget  The handler that receives the open message.
 * @param maxItems    Maximum number of recent items to display.
 */
RecentItemsMenu::RecentItemsMenu(const char* title, BMessage* openMessage,
	BHandler* itemTarget, int32 maxItems)
	:
	BSlowMenu(title),
	fIterator(NULL),
	fTargetMesage(openMessage),
	fItemTarget(itemTarget),
	fCount(-1),
	fSanityCount(-1),
	fMaxCount(maxItems)
{
}


/**
 * @brief Destroy the RecentItemsMenu, releasing the iterator and open message.
 */
RecentItemsMenu::~RecentItemsMenu()
{
	delete fIterator;
	delete fTargetMesage;
}


/**
 * @brief Add the next recent item to the menu.
 *
 * Calls fIterator->GetNextMenuItem() and appends the result.  Stops when
 * fMaxCount items have been added or after fMaxCount + 20 sanity iterations
 * (to handle stale recent-apps entries).
 *
 * @return True if the caller should invoke AddNextItem() again.
 */
bool
RecentItemsMenu::AddNextItem()
{
	BMenuItem* item = fIterator->GetNextMenuItem(FileMessage(),
		ContainerMessage(), fItemTarget);
	if (item != NULL) {
		AddItem(item);
		fCount++;
	}
	fSanityCount++;

	return fCount < fMaxCount - 1 && (fSanityCount < fMaxCount + 20);
		// fSanityCount is a hacky way of dealing with a lot of stale
		// recent apps
}


/**
 * @brief Clear old items and rewind the iterator before a fresh build.
 *
 * @return True to indicate the build should proceed.
 */
bool
RecentItemsMenu::StartBuildingItemList()
{
	// remove any preexisting items
	int32 itemCount = CountItems();
	while (itemCount--)
		delete RemoveItem((int32)0);

	fCount = 0;
	fSanityCount = 0;
	fIterator->Rewind();

	return true;
}


/**
 * @brief Reset fMenuBuilt so the item list is rebuilt on the next open.
 */
void
RecentItemsMenu::ClearMenuBuildingState()
{
	fMenuBuilt = false;
		// force rebuilding each time
	fIterator->Rewind();
}


// #pragma mark - RecentFilesMenu


/**
 * @brief Construct a RecentFilesMenu filtered by a single MIME type.
 *
 * @param title             The menu title.
 * @param openFileMessage   Message sent when a file item is activated.
 * @param openFolderMessage Message sent when a folder item is activated.
 * @param target            Handler that receives the messages.
 * @param maxItems          Maximum number of items to show.
 * @param navMenuFolders    If true, folders show a hierarchical nav menu.
 * @param ofType            Optional MIME type filter string.
 * @param openedByAppSig    Optional app-signature filter.
 */
RecentFilesMenu::RecentFilesMenu(const char* title, BMessage* openFileMessage,
	BMessage* openFolderMessage, BHandler* target, int32 maxItems,
	bool navMenuFolders, const char* ofType, const char* openedByAppSig)
	:
	RecentItemsMenu(title, openFileMessage, target, maxItems),
	openFolderMessage(openFolderMessage)
{
	fIterator = new BRecentFilesList(maxItems + 10, navMenuFolders,
		ofType, openedByAppSig);
}


/**
 * @brief Construct a RecentFilesMenu filtered by a list of MIME types.
 *
 * @param title             The menu title.
 * @param openFileMessage   Message sent when a file item is activated.
 * @param openFolderMessage Message sent when a folder item is activated.
 * @param target            Handler that receives the messages.
 * @param maxItems          Maximum number of items to show.
 * @param navMenuFolders    If true, folders show a hierarchical nav menu.
 * @param ofTypeList        Array of MIME type strings to filter by.
 * @param ofTypeListCount   Number of entries in @p ofTypeList.
 * @param openedByAppSig    Optional app-signature filter.
 */
RecentFilesMenu::RecentFilesMenu(const char* title, BMessage* openFileMessage,
	BMessage* openFolderMessage, BHandler* target, int32 maxItems,
	bool navMenuFolders, const char* ofTypeList[], int32 ofTypeListCount,
	const char* openedByAppSig)
	:
	RecentItemsMenu(title, openFileMessage, target, maxItems),
	openFolderMessage(openFolderMessage)
{
	fIterator = new BRecentFilesList(maxItems + 10, navMenuFolders,
		ofTypeList, ofTypeListCount, openedByAppSig);
}


/**
 * @brief Destroy the RecentFilesMenu, releasing the folder-open message.
 */
RecentFilesMenu::~RecentFilesMenu()
{
	delete openFolderMessage;
}


// #pragma mark - RecentFoldersMenu


/**
 * @brief Construct a RecentFoldersMenu.
 *
 * @param title           The menu title.
 * @param openMessage     Message sent when a folder item is activated.
 * @param target          Handler that receives the messages.
 * @param maxItems        Maximum number of items to show.
 * @param navMenuFolders  If true, items have a hierarchical nav sub-menu.
 * @param openedByAppSig  Optional app-signature filter.
 */
RecentFoldersMenu::RecentFoldersMenu(const char* title, BMessage* openMessage,
	BHandler* target, int32 maxItems, bool navMenuFolders,
	const char* openedByAppSig)
	:
	RecentItemsMenu(title, openMessage, target, maxItems)
{
	fIterator = new BRecentFoldersList(maxItems + 10, navMenuFolders,
		openedByAppSig);
}


// #pragma mark - RecentAppsMenu


/**
 * @brief Construct a RecentAppsMenu.
 *
 * @param title       The menu title.
 * @param openMessage Message sent when an application item is activated.
 * @param target      Handler that receives the messages.
 * @param maxItems    Maximum number of items to show.
 */
RecentAppsMenu::RecentAppsMenu(const char* title, BMessage* openMessage,
	BHandler* target, int32 maxItems)
	:
	RecentItemsMenu(title, openMessage, target, maxItems)
{
	fIterator = new BRecentAppsList(maxItems);
}


// #pragma mark - BRecentItemsList


/**
 * @brief Construct a BRecentItemsList with the given item limit.
 *
 * @param maxItems       Maximum number of items to enumerate.
 * @param navMenuFolders If true, folder items carry a nav sub-menu.
 */
BRecentItemsList::BRecentItemsList(int32 maxItems, bool navMenuFolders)
	:
	fMaxItems(maxItems),
	fNavMenuFolders(navMenuFolders)
{
	InitIconPreloader();
		// need the icon cache
	Rewind();
}


/**
 * @brief Reset the iteration index and clear the item cache.
 */
void
BRecentItemsList::Rewind()
{
	fIndex = 0;
	fItems.MakeEmpty();
}


/**
 * @brief Return the next icon menu item from the recent-items list.
 *
 * Calls GetNextRef(), builds a Model, and creates a ModelMenuItem (or
 * BNavMenu item for containers when navMenuFolders is enabled).
 *
 * @param fileOpenInvokeMessage       Message sent when a file item is chosen.
 * @param containerOpenInvokeMessage  Message sent when a folder item is chosen.
 * @param target                      Handler that receives the messages.
 * @param currentItemRef              Optional output for the ref of the returned item.
 * @return A new BMenuItem on success, or NULL when the list is exhausted.
 */
BMenuItem*
BRecentItemsList::GetNextMenuItem(const BMessage* fileOpenInvokeMessage,
	const BMessage* containerOpenInvokeMessage, BHandler* target,
	entry_ref* currentItemRef)
{
	entry_ref ref;
	if (GetNextRef(&ref) != B_OK)
		return NULL;

	Model model(&ref, true);
	if (model.InitCheck() != B_OK)
		return NULL;

	bool container = false;
	if (model.IsSymLink()) {

		Model* newResolvedModel = NULL;
		Model* result = model.LinkTo();

		if (result == NULL) {
			newResolvedModel = new Model(model.EntryRef(), true, true);

			if (newResolvedModel->InitCheck() != B_OK) {
				// broken link, still can show though, bail
				delete newResolvedModel;
				result = NULL;
			} else
				result = newResolvedModel;
		} else {
			BModelOpener opener(result);
				// open the model, if it ain't open already

			PoseInfo poseInfo;
			BNode* resultNode = result->Node();
			if (resultNode != NULL) {
				resultNode->ReadAttr(kAttrPoseInfo, B_RAW_TYPE, 0,
					&poseInfo, sizeof(poseInfo));
			}

			result->CloseNode();

			ref = *result->EntryRef();
			container = result->IsContainer();
		}
		model.SetLinkTo(result);
	} else {
		ref = *model.EntryRef();
		container = model.IsContainer();
	}

	// if user asked for it, return the current item ref
	if (currentItemRef != NULL)
		*currentItemRef = ref;

	BMessage* message;
	if (container && containerOpenInvokeMessage)
		message = new BMessage(*containerOpenInvokeMessage);
	else if (!container && fileOpenInvokeMessage)
		message = new BMessage(*fileOpenInvokeMessage);
	else
		message = new BMessage(B_REFS_RECEIVED);

	message->AddRef("refs", model.EntryRef());

	// menu font
	menu_info info;
	get_menu_info(&info);
	BFont menuFont;
	menuFont.SetFamilyAndStyle(info.f_family, info.f_style);
	menuFont.SetSize(info.font_size);

	// Truncate the name if necessary
	BString truncatedString(model.Name());
	menuFont.TruncateString(&truncatedString, B_TRUNCATE_END, BNavMenu::GetMaxMenuWidth());

	ModelMenuItem* item = NULL;
	if (!container || !fNavMenuFolders)
		item = new ModelMenuItem(&model, truncatedString.String(), message);
	else {
		// add another nav menu item if it's a directory
		BNavMenu* menu = new BNavMenu(truncatedString.String(), message->what,
			target, 0);

		menu->SetNavDir(&ref);
		item = new ModelMenuItem(&model, menu);
		item->SetMessage(message);
	}

	if (item != NULL && target != NULL)
		item->SetTarget(target);

	return item;
}


status_t
BRecentItemsList::GetNextRef(entry_ref* result)
{
	return fItems.FindRef("refs", fIndex++, result);
}


// #pragma mark - BRecentFilesList


BRecentFilesList::BRecentFilesList(int32 maxItems, bool navMenuFolders,
	const char* ofType, const char* openedByAppSig)
	:
	BRecentItemsList(maxItems, navMenuFolders),
	fType(ofType),
	fTypes(NULL),
	fTypeCount(0),
	fAppSig(openedByAppSig)
{
}


BRecentFilesList::BRecentFilesList(int32 maxItems, bool navMenuFolders,
	const char* ofTypeList[], int32 ofTypeListCount,
	const char* openedByAppSig)
	:
	BRecentItemsList(maxItems, navMenuFolders),
	fType(NULL),
	fTypes(NULL),
	fTypeCount(ofTypeListCount),
	fAppSig(openedByAppSig)
{
	if (fTypeCount > 0) {
		fTypes = new char *[ofTypeListCount];
		for (int32 index = 0; index < ofTypeListCount; index++)
			fTypes[index] = strdup(ofTypeList[index]);
	}
}


BRecentFilesList::~BRecentFilesList()
{
	if (fTypeCount > 0) {
		for (int32 index = 0; index < fTypeCount; index++)
			free(fTypes[index]);
		delete[] fTypes;
	}
}


status_t
BRecentFilesList::GetNextRef(entry_ref* ref)
{
	if (fIndex == 0) {
		// Lazy roster Get
		if (fTypes != NULL) {
			BRoster().GetRecentDocuments(&fItems, fMaxItems,
				const_cast<const char**>(fTypes),
				fTypeCount, fAppSig.Length() ? fAppSig.String() : NULL);
		} else {
			BRoster().GetRecentDocuments(&fItems, fMaxItems,
				fType.Length() ? fType.String() : NULL,
				fAppSig.Length() ? fAppSig.String() : NULL);
		}

	}

	return BRecentItemsList::GetNextRef(ref);
}


BMenu*
BRecentFilesList::NewFileListMenu(const char* title,
	BMessage* openFileMessage, BMessage* openFolderMessage,
	BHandler* target, int32 maxItems, bool navMenuFolders, const char* ofType,
	const char* openedByAppSig)
{
	return new RecentFilesMenu(title, openFileMessage,
		openFolderMessage, target, maxItems, navMenuFolders, ofType,
		openedByAppSig);
}


BMenu*
BRecentFilesList::NewFileListMenu(const char* title,
	BMessage* openFileMessage, BMessage* openFolderMessage,
	BHandler* target, int32 maxItems, bool navMenuFolders,
	const char* ofTypeList[], int32 ofTypeListCount,
	const char* openedByAppSig)
{
	return new RecentFilesMenu(title, openFileMessage,
		openFolderMessage, target, maxItems, navMenuFolders, ofTypeList,
		ofTypeListCount, openedByAppSig);
}


// #pragma mark - BRecentFoldersList


BMenu*
BRecentFoldersList::NewFolderListMenu(const char* title,
	BMessage* openMessage, BHandler* target, int32 maxItems,
	bool navMenuFolders, const char* openedByAppSig)
{
	return new RecentFoldersMenu(title, openMessage, target, maxItems,
		navMenuFolders, openedByAppSig);
}


BRecentFoldersList::BRecentFoldersList(int32 maxItems, bool navMenuFolders,
	const char* openedByAppSig)
	:
	BRecentItemsList(maxItems, navMenuFolders),
	fAppSig(openedByAppSig)
{
}


status_t
BRecentFoldersList::GetNextRef(entry_ref* ref)
{
	if (fIndex == 0) {
		// Lazy roster Get
		BRoster().GetRecentFolders(&fItems, fMaxItems,
			fAppSig.Length() ? fAppSig.String() : NULL);

	}

	return BRecentItemsList::GetNextRef(ref);
}


// #pragma mark - BRecentAppsList


BRecentAppsList::BRecentAppsList(int32 maxItems)
	:
	BRecentItemsList(maxItems, false)
{
}


status_t
BRecentAppsList::GetNextRef(entry_ref* ref)
{
	if (fIndex == 0) {
		// Lazy roster Get
		BRoster().GetRecentApps(&fItems, fMaxItems);
	}

	return BRecentItemsList::GetNextRef(ref);
}


BMenu*
BRecentAppsList::NewAppListMenu(const char* title, BMessage* openMessage,
	 BHandler* target, int32 maxItems)
{
	return new RecentAppsMenu(title, openMessage, target, maxItems);
}
