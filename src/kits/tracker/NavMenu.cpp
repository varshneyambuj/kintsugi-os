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
 * @file NavMenu.cpp
 * @brief BNavMenu — a hierarchical, lazily-built navigation menu for Tracker.
 *
 * BNavMenu is a BSlowMenu subclass that populates its items incrementally to
 * remain fully interruptible.  It displays icons and names for the immediate
 * children of a directory, volume, or query container.  Spring-loaded folder
 * helpers are also provided for enabling/disabling items during drag-and-drop.
 *
 * @see BSlowMenu, BContainerWindow, BPoseView
 */


#include "NavMenu.h"

#include <algorithm>

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <Application.h>
#include <Catalog.h>
#include <Debug.h>
#include <Directory.h>
#include <Locale.h>
#include <Path.h>
#include <Query.h>
#include <Screen.h>
#include <StopWatch.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include "Attributes.h"
#include "Commands.h"
#include "ContainerWindow.h"
#include "DesktopPoseView.h"
#include "FunctionObject.h"
#include "FSUtils.h"
#include "IconMenuItem.h"
#include "MimeTypes.h"
#include "PoseView.h"
#include "QueryPoseView.h"
#include "Thread.h"
#include "Tracker.h"
#include "VirtualDirectoryEntryList.h"


namespace BPrivate {

const int32 kMinMenuWidth = 150;

enum nav_flags {
	kVolumesOnly = 1,
	kShowParent = 2
};


/**
 * @brief Check whether two drag messages refer to the same set of refs and click point.
 *
 * Returns true only if every "refs" entry in @p incoming matches a ref in
 * @p dragMessage and the "click_pt" fields are identical, indicating this is
 * not a new drag but the same one.
 *
 * @param incoming     The newly-arrived drag message.
 * @param dragMessage  The previously-seen drag message.
 * @return True if the messages represent the same drag payload.
 */
bool
SpringLoadedFolderCompareMessages(const BMessage* incoming, const BMessage* dragMessage)
{
	if (incoming == NULL || dragMessage == NULL)
		return false;

	bool refsMatch = false;
	for (int32 inIndex = 0; incoming->HasRef("refs", inIndex); inIndex++) {
		entry_ref inRef;
		if (incoming->FindRef("refs", inIndex, &inRef) != B_OK) {
			refsMatch = false;
			break;
		}

		bool inRefMatch = false;
		for (int32 dragIndex = 0; dragMessage->HasRef("refs", dragIndex);
			dragIndex++) {
			entry_ref dragRef;
			if (dragMessage->FindRef("refs", dragIndex, &dragRef) != B_OK) {
				inRefMatch =  false;
				break;
			}
			// if the incoming ref matches any ref in the drag ref
			// then we can try the next incoming ref
			if (inRef == dragRef) {
				inRefMatch = true;
				break;
			}
		}
		refsMatch = inRefMatch;
		if (!inRefMatch)
			break;
	}

	if (refsMatch) {
		// If all the refs match try and see if this is a new drag with
		// the same drag contents.
		refsMatch = false;
		BPoint incomingPoint;
		BPoint dragPoint;
		if (incoming->FindPoint("click_pt", &incomingPoint) == B_OK
			&& dragMessage->FindPoint("click_pt", &dragPoint) == B_OK) {
			refsMatch = (incomingPoint == dragPoint);
		}
	}

	return refsMatch;
}


/**
 * @brief Enable/disable menu items based on whether they can accept the dragged types.
 *
 * Iterates ModelMenuItems in @p menu and calls SupportsMimeType() on their
 * models.  Directories and volumes are always enabled; files and executables
 * are enabled only if they support at least one type in @p typeslist.
 *
 * @param menu       The menu whose items are to be updated.
 * @param typeslist  List of MIME type strings from the drag payload.
 */
void
SpringLoadedFolderSetMenuStates(const BMenu* menu,
	const BStringList* typeslist)
{
	if (menu == NULL || typeslist == NULL || typeslist->IsEmpty())
		return;

	// If a types list exists iterate through the list and see if each item
	// can support any item in the list and set the enabled state of the item.
	int32 count = menu->CountItems();
	for (int32 index = 0 ; index < count ; index++) {
		ModelMenuItem* item = dynamic_cast<ModelMenuItem*>(menu->ItemAt(index));
		if (item == NULL)
			continue;

		const Model* model = item->TargetModel();
		if (!model)
			continue;

		if (model->IsSymLink()) {
			// find out what the model is, resolve if symlink
			BEntry entry(model->EntryRef(), true);
			if (entry.InitCheck() == B_OK) {
				if (entry.IsDirectory()) {
					// folder? always keep enabled
					item->SetEnabled(true);
				} else {
					// other, check its support
					Model resolvedModel(&entry);
					int32 supported
						= resolvedModel.SupportsMimeType(NULL, typeslist);
					item->SetEnabled(supported != kDoesNotSupportType);
				}
			} else {
				// bad entry ref (bad symlink?), disable
				item->SetEnabled(false);
			}
		} else if (model->IsDirectory() || model->IsRoot() || model->IsVolume())
			// always enabled if a container
			item->SetEnabled(true);
		else if (model->IsFile() || model->IsExecutable()) {
			int32 supported = model->SupportsMimeType(NULL, typeslist);
			item->SetEnabled(supported != kDoesNotSupportType);
		} else
			item->SetEnabled(false);
	}
}


/**
 * @brief Add the MIME type of @p ref to @p typeslist, avoiding duplicates.
 *
 * For symlinks the target's type is added instead.  Does nothing if @p ref
 * or @p typeslist is NULL, or if the node has no valid MIME type.
 *
 * @param ref        The entry whose MIME type is to be added.
 * @param typeslist  The list to append to.
 */
void
SpringLoadedFolderAddUniqueTypeToList(entry_ref* ref,
	BStringList* typeslist)
{
	if (ref == NULL || typeslist == NULL)
		return;

	// get the mime type for the current ref
	BNodeInfo nodeinfo;
	BNode node(ref);
	if (node.InitCheck() != B_OK)
		return;

	nodeinfo.SetTo(&node);

	char mimestr[B_MIME_TYPE_LENGTH];
	// add it to the list
	if (nodeinfo.GetType(mimestr) == B_OK && strlen(mimestr) > 0) {
		// If this is a symlink, add symlink to the list (below)
		// resolve the symlink, add the resolved type to the list.
		if (strcmp(B_LINK_MIMETYPE, mimestr) == 0) {
			BEntry entry(ref, true);
			if (entry.InitCheck() == B_OK) {
				entry_ref resolvedRef;
				if (entry.GetRef(&resolvedRef) == B_OK)
					SpringLoadedFolderAddUniqueTypeToList(&resolvedRef,
						typeslist);
			}
		}
		// scan the current list, don't add dups
		bool isUnique = true;
		int32 count = typeslist->CountStrings();
		for (int32 index = 0 ; index < count ; index++) {
			if (typeslist->StringAt(index).Compare(mimestr) == 0) {
				isUnique = false;
				break;
			}
		}

		if (isUnique)
			typeslist->Add(mimestr);
	}
}


/**
 * @brief Cache a drag message and build the corresponding MIME-type list.
 *
 * Replaces *message and *typeslist with newly-allocated objects derived from
 * @p incoming so spring-loaded folder checks can be done without re-parsing
 * the message on each mouse-over event.
 *
 * @param incoming   The drag BMessage to cache; may be NULL (clears state).
 * @param message    Output: receives a copy of @p incoming.
 * @param typeslist  Output: receives the list of unique MIME types from the drag refs.
 */
void
SpringLoadedFolderCacheDragData(const BMessage* incoming, BMessage** message,
	BStringList** typeslist)
{
	if (incoming == NULL)
		return;

	delete* message;
	delete* typeslist;

	BMessage* localMessage = new BMessage(*incoming);
	BStringList* localTypesList = new BStringList(10);

	for (int32 index = 0; incoming->HasRef("refs", index); index++) {
		entry_ref ref;
		if (incoming->FindRef("refs", index, &ref) != B_OK)
			continue;

		SpringLoadedFolderAddUniqueTypeToList(&ref, localTypesList);
	}

	*message = localMessage;
	*typeslist = localTypesList;
}

} // namespace BPrivate


//	#pragma mark - BNavMenu


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "NavMenu"


/**
 * @brief Construct a BNavMenu for navigating the children of a directory.
 *
 * @param title         The menu title string.
 * @param message       The what-code sent when an item is selected.
 * @param target        The handler that receives item messages.
 * @param parentWindow  Optional window whose location constrains the menu.
 * @param list          Optional list of accepted MIME types for spring-loading.
 */
BNavMenu::BNavMenu(const char* title, uint32 message, const BHandler* target,
	BWindow* parentWindow, const BStringList* list)
	:
	BSlowMenu(title),
	fMessage(message),
	fMessenger(target, target->Looper()),
	fParentWindow(parentWindow),
	fFlags(0),
	fItemList(NULL),
	fContainer(NULL),
	fIteratingDesktop(false),
	fTypesList(new BStringList(10))
{
	if (list != NULL)
		*fTypesList = *list;

	InitIconPreloader();

	// add the parent window to the invocation message so that it
	// can be closed if option modifier held down during invocation
	BContainerWindow* source = dynamic_cast<BContainerWindow*>(fParentWindow);
	if (source != NULL) {
		fMessage.AddData("nodeRefsToClose", B_RAW_TYPE,
			source->TargetModel()->NodeRef(), sizeof(node_ref));
	}

	// too long to have triggers
	SetTriggersEnabled(false);
}


BNavMenu::BNavMenu(const char* title, uint32 message,
	const BMessenger& messenger, BWindow* parentWindow,
	const BStringList* list)
	:
	BSlowMenu(title),
	fMessage(message),
	fMessenger(messenger),
	fParentWindow(parentWindow),
	fFlags(0),
	fItemList(NULL),
	fContainer(NULL),
	fIteratingDesktop(false),
	fTypesList(new BStringList(10))
{
	if (list != NULL)
		*fTypesList = *list;

	InitIconPreloader();

	// add the parent window to the invocation message so that it
	// can be closed if option modifier held down during invocation
	BContainerWindow* source = dynamic_cast<BContainerWindow*>(fParentWindow);
	if (source != NULL) {
		fMessage.AddData("nodeRefsToClose", B_RAW_TYPE,
			source->TargetModel()->NodeRef(), sizeof(node_ref));
	}

	// too long to have triggers
	SetTriggersEnabled(false);
}


BNavMenu::~BNavMenu()
{
	delete fTypesList;
}


void
BNavMenu::AttachedToWindow()
{
	BSlowMenu::AttachedToWindow();

	SpringLoadedFolderSetMenuStates(this, fTypesList);
		// If dragging, (fTypesList != NULL) set the menu items enabled state
		// relative to the ability to handle an item in the drag message.
	ResetTargets();
		// allow an opportunity to reset the target for each of the items
}


void
BNavMenu::DetachedFromWindow()
{
}


void
BNavMenu::ResetTargets()
{
	SetTargetForItems(Target());
}


void
BNavMenu::ForceRebuild()
{
	ClearMenuBuildingState();
	fMenuBuilt = false;
}


bool
BNavMenu::NeedsToRebuild() const
{
	return !fMenuBuilt;
}


void
BNavMenu::SetNavDir(const entry_ref* ref)
{
	ForceRebuild();
		// reset the slow menu building mechanism so we can add more stuff

	fNavDir = *ref;
}


void
BNavMenu::ClearMenuBuildingState()
{
	delete fContainer;
	fContainer = NULL;

	// item list is non-owning, need to delete the items because
	// they didn't get added to the menu
	if (fItemList != NULL) {
		RemoveItems(0, fItemList->CountItems(), true);

		delete fItemList;
		fItemList = NULL;
	}
}


bool
BNavMenu::StartBuildingItemList()
{
	BEntry entry;

	if (fNavDir.device < 0 || entry.SetTo(&fNavDir, true) != B_OK
		|| !entry.Exists()) {
		return false;
	}

	fItemList = new BObjectList<BMenuItem>(50);

	fIteratingDesktop = false;

	BDirectory parent;
	status_t status = entry.GetParent(&parent);

	// if ref is the root item then build list of volume root dirs
	fFlags = uint8((fFlags & ~kVolumesOnly) | (status == B_ENTRY_NOT_FOUND ? kVolumesOnly : 0));
	if ((fFlags & kVolumesOnly) != 0)
		return true;

	Model startModel(&entry, true);
	if (startModel.InitCheck() != B_OK || !startModel.IsContainer())
		return false;

	if (startModel.IsQuery()) {
		fContainer = new QueryEntryListCollection(&startModel);
	} else if (startModel.IsVirtualDirectory()) {
		fContainer = new VirtualDirectoryEntryList(&startModel);
	} else if (startModel.IsDesktop()) {
		fIteratingDesktop = true;
		fContainer = DesktopPoseView::InitDesktopDirentIterator(0, startModel.EntryRef());
		if (TrackerSettings().MountVolumesOntoDesktop())
			AddVolumeItems();
		else if (TrackerSettings().ShowDisksIcon())
			AddRootItem();

		AddTrashItem();
	} else if (startModel.IsTrash()) {
		// the trash window needs to display a union of all the
		// trash folders from all the mounted volumes
		BVolumeRoster volRoster;
		volRoster.Rewind();
		BVolume volume;
		fContainer = new EntryIteratorList();

		while (volRoster.GetNextVolume(&volume) == B_OK) {
			if (volume.IsReadOnly() || !volume.IsPersistent() || volume.Capacity() == 0)
				continue;

			BDirectory trashDir;
			if (FSGetTrashDir(&trashDir, volume.Device()) == B_OK) {
				EntryIteratorList* iteratorList = dynamic_cast<EntryIteratorList*>(fContainer);
				ASSERT(iteratorList != NULL);
				if (iteratorList != NULL)
					iteratorList->AddItem(new DirectoryEntryList(trashDir));
			}
		}
	} else {
		BDirectory* directory = dynamic_cast<BDirectory*>(startModel.Node());
		ASSERT(directory != NULL);
		if (directory != NULL)
			fContainer = new DirectoryEntryList(*directory);
	}

	if (fContainer == NULL || fContainer->InitCheck() != B_OK)
		return false;

	fContainer->Rewind();

	return true;
}


void
BNavMenu::AddRootItem()
{
	BEntry entry("/");
	Model model(&entry);
	if (model.InitCheck() != B_OK)
		return;

	AddOneItem(&model);
}


void
BNavMenu::AddVolumeItems()
{
	BVolumeRoster roster;
	roster.Rewind();

	BVolume volume;
	BDirectory root;
	BEntry entry;
	Model model;

	while (roster.GetNextVolume(&volume) == B_OK) {
		if (volume.InitCheck() != B_OK || !volume.IsPersistent() || volume.Capacity() == 0
			|| volume.GetRootDirectory(&root) != B_OK || root.GetEntry(&entry) != B_OK) {
			continue;
		}

		model.SetTo(&entry);
		AddOneItem(&model);
	}
}


void
BNavMenu::AddTrashItem()
{
	BPath path;
	if (find_directory(B_TRASH_DIRECTORY, &path) == B_OK) {
		BEntry entry(path.Path());
		Model model(&entry);
		AddOneItem(&model);
	}
}


bool
BNavMenu::AddNextItem()
{
	if ((fFlags & kVolumesOnly) != 0) {
		BuildVolumeMenu();
		return false;
	}

	BEntry entry;
	if (fContainer->GetNextEntry(&entry) != B_OK) {
		// we're finished
		return false;
	}

	if (TrackerSettings().HideDotFiles()) {
		char name[B_FILE_NAME_LENGTH];
		if (entry.GetName(name) == B_OK && name[0] == '.')
			return true;
	}

	Model model(&entry, true);
	if (model.InitCheck() != B_OK) {
//		PRINT(("not showing hidden item %s, wouldn't open\n", model->Name()));
		return true;
	}

	// skip Trash
	if (model.IsTrash())
		return true;

	QueryEntryListCollection* queryContainer = dynamic_cast<QueryEntryListCollection*>(fContainer);
	if (queryContainer != NULL && !queryContainer->ShowResultsFromTrash()
		&& FSInTrashDir(model.EntryRef())) {
		// query entry is in trash and shall not be shown
		return true;
	}

	ssize_t size = -1;
	PoseInfo poseInfo;
	if (model.Node() != NULL)
		size = model.Node()->ReadAttr(kAttrPoseInfo, B_RAW_TYPE, 0, &poseInfo, sizeof(poseInfo));

	model.CloseNode();

	// item might be in invisible
	if (size == sizeof(poseInfo) && !BPoseView::PoseVisible(&model, &poseInfo))
		return true;

	AddOneItem(&model);

	return true;
}


void
BNavMenu::AddOneItem(Model* model)
{
	BMenuItem* item = NewModelItem(model, &fMessage, fMessenger, false,
		dynamic_cast<BContainerWindow*>(fParentWindow),
		fTypesList, &fTrackingHook);

	if (item != NULL)
		fItemList->AddItem(item);
}


ModelMenuItem*
BNavMenu::NewModelItem(Model* model, const BMessage* invokeMessage,
	const BMessenger& target, bool suppressFolderHierarchy,
	BContainerWindow* parentWindow, const BStringList* typeslist,
	TrackingHookData* hook)
{
	if (model->InitCheck() != B_OK)
		return 0;

	entry_ref ref;
	bool isContainer = false;
	if (model->IsSymLink()) {
		Model* newResolvedModel = 0;
		Model* result = model->LinkTo();

		if (result == NULL) {
			newResolvedModel = new Model(model->EntryRef(), true, true);

			if (newResolvedModel->InitCheck() != B_OK) {
				// broken link, still can show though, bail
				delete newResolvedModel;
				result = NULL;
			} else
				result = newResolvedModel;
		}

		if (result != NULL) {
			BModelOpener opener(result);
				// open the model, if it ain't open already

			PoseInfo poseInfo;
			ssize_t size = -1;

			if (result->Node() != NULL) {
				size = result->Node()->ReadAttr(kAttrPoseInfo, B_RAW_TYPE, 0,
					&poseInfo, sizeof(poseInfo));
			}

			result->CloseNode();

			if (size == sizeof(poseInfo) && !BPoseView::PoseVisible(result,
				&poseInfo)) {
				// link target does not want to be visible
				delete newResolvedModel;
				return NULL;
			}

			ref = *result->EntryRef();
			isContainer = result->IsContainer();
		}

		model->SetLinkTo(result);
	} else {
		ref = *model->EntryRef();
		isContainer = model->IsContainer();
	}

	BMessage* message = new BMessage(*invokeMessage);
	message->AddRef("refs", model->EntryRef());

	menu_info info;
	get_menu_info(&info);
	BFont menuFont;
	menuFont.SetFamilyAndStyle(info.f_family, info.f_style);
	menuFont.SetSize(info.font_size);

	// truncate name if necessary
	BString truncatedString(model->Name());
	menuFont.TruncateString(&truncatedString, B_TRUNCATE_END, GetMaxMenuWidth());

	ModelMenuItem* item = NULL;
	if (!isContainer || suppressFolderHierarchy) {
		item = new ModelMenuItem(model, truncatedString.String(), message);
		if (invokeMessage->what != B_REFS_RECEIVED)
			item->SetEnabled(false);
			// the above is broken for FavoritesMenu::AddNextItem, which uses a
			// workaround - should fix this
	} else {
		BNavMenu* menu = new BNavMenu(truncatedString.String(),
			invokeMessage->what, target, parentWindow, typeslist);
		menu->SetNavDir(&ref);
		if (hook != NULL) {
			menu->InitTrackingHook(hook->fTrackingHook, &(hook->fTarget),
				hook->fDragMessage);
		}

		item = new ModelMenuItem(model, menu);
		item->SetMessage(message);
	}

	return item;
}


void
BNavMenu::BuildVolumeMenu()
{
	BVolumeRoster roster;
	roster.Rewind();

	BVolume volume;
	BDirectory startDir;
	BEntry entry;

	while (roster.GetNextVolume(&volume) == B_OK) {
		if (volume.InitCheck() != B_OK || !volume.IsPersistent() || volume.Capacity() == 0)
			continue;

		if (volume.GetRootDirectory(&startDir) == B_OK) {
			startDir.GetEntry(&entry);

			Model* model = new Model(&entry);
			if (model->InitCheck() != B_OK) {
				delete model;
				continue;
			}

			BNavMenu* menu = new BNavMenu(model->Name(), fMessage.what,
				fMessenger, fParentWindow, fTypesList);

			menu->SetNavDir(model->EntryRef());

			ASSERT(menu->Name() != NULL);

			ModelMenuItem* item = new ModelMenuItem(model, menu);
			BMessage* message = new BMessage(fMessage);

			message->AddRef("refs", model->EntryRef());

			item->SetMessage(message);
			fItemList->AddItem(item);
			ASSERT(item->Label() != NULL);
		}
	}
}


int
BNavMenu::CompareFolderNamesFirstOne(const BMenuItem* i1, const BMenuItem* i2)
{
	ThrowOnAssert(i1 != NULL && i2 != NULL);

	const ModelMenuItem* item1 = dynamic_cast<const ModelMenuItem*>(i1);
	const ModelMenuItem* item2 = dynamic_cast<const ModelMenuItem*>(i2);

	if (item1 != NULL && item2 != NULL)
		return item1->TargetModel()->CompareFolderNamesFirst(item2->TargetModel());

	return CompareOne(i1, i2);
}


int
BNavMenu::CompareOne(const BMenuItem* i1, const BMenuItem* i2)
{
	ThrowOnAssert(i1 != NULL && i2 != NULL);

	return NaturalCompare(i1->Label(), i2->Label());
}


void
BNavMenu::DoneBuildingItemList()
{
	// add sorted items to menu
	if (TrackerSettings().SortFolderNamesFirst())
		fItemList->SortItems(CompareFolderNamesFirstOne);
	else
		fItemList->SortItems(CompareOne);

	// if the parent link should be shown, it will be the first
	// entry in the menu - but don't add the item if we're already
	// at the file system's root
	if ((fFlags & kShowParent) != 0) {
		BDirectory directory(&fNavDir);
		BEntry entry(&fNavDir);
		if (!directory.IsRootDirectory() && entry.GetParent(&entry) == B_OK) {
			Model model(&entry, true);
			BLooper* looper;
			AddNavParentDir(&model, fMessage.what, fMessenger.Target(&looper));
		}
	}

	int32 count = fItemList->CountItems();
	for (int32 index = 0; index < count; index++)
		AddItem(fItemList->ItemAt(index));

	fItemList->MakeEmpty();

	if (count == 0) {
		BMenuItem* item = new BMenuItem(B_TRANSLATE("Empty folder"), 0);
		item->SetEnabled(false);
		AddItem(item);
	}

	SetTargetForItems(fMessenger);
}


int32
BNavMenu::GetMaxMenuWidth(void)
{
	return std::max((int32)(BScreen().Frame().Width() / 4), kMinMenuWidth);
}


void
BNavMenu::AddNavDir(const Model* model, uint32 what, BHandler* target,
	bool populateSubmenu)
{
	BMessage* message = new BMessage((uint32)what);
	message->AddRef("refs", model->EntryRef());
	ModelMenuItem* item = NULL;

	if (populateSubmenu) {
		BNavMenu* navMenu = new BNavMenu(model->Name(), what, target);
		navMenu->SetNavDir(model->EntryRef());
		navMenu->InitTrackingHook(fTrackingHook.fTrackingHook,
			&(fTrackingHook.fTarget), fTrackingHook.fDragMessage);
		item = new ModelMenuItem(model, navMenu);
		item->SetMessage(message);
	} else
		item = new ModelMenuItem(model, model->Name(), message);

	AddItem(item);
}


void
BNavMenu::AddNavParentDir(const char* name,const Model* model,
	uint32 what, BHandler* target)
{
	BNavMenu* menu = new BNavMenu(name, what, target);
	menu->SetNavDir(model->EntryRef());
	menu->SetShowParent(true);
	menu->InitTrackingHook(fTrackingHook.fTrackingHook,
		&(fTrackingHook.fTarget), fTrackingHook.fDragMessage);

	BMenuItem* item = new SpecialModelMenuItem(model, menu);
	BMessage* message = new BMessage(what);
	message->AddRef("refs", model->EntryRef());
	item->SetMessage(message);

	AddItem(item);
}


void
BNavMenu::AddNavParentDir(const Model* model, uint32 what, BHandler* target)
{
	AddNavParentDir(B_TRANSLATE("parent folder"),model, what, target);
}


void
BNavMenu::SetShowParent(bool show)
{
	fFlags = uint8((fFlags & ~kShowParent) | (show ? kShowParent : 0));
}


void
BNavMenu::SetTypesList(const BStringList* list)
{
	if (list != NULL)
		*fTypesList = *list;
	else
		fTypesList->MakeEmpty();
}


const BStringList*
BNavMenu::TypesList() const
{
	return fTypesList;
}


void
BNavMenu::SetTarget(const BMessenger& messenger)
{
	fMessenger = messenger;
}


BMessenger
BNavMenu::Target()
{
	return fMessenger;
}


TrackingHookData*
BNavMenu::InitTrackingHook(bool (*hook)(BMenu*, void*),
	const BMessenger* target, const BMessage* dragMessage)
{
	fTrackingHook.fTrackingHook = hook;
	if (target != NULL)
		fTrackingHook.fTarget = *target;

	fTrackingHook.fDragMessage = dragMessage;
	SetTrackingHookDeep(this, hook, &fTrackingHook);

	return &fTrackingHook;
}


void
BNavMenu::SetTrackingHookDeep(BMenu* menu, bool (*func)(BMenu*, void*),
	void* state)
{
	menu->SetTrackingHook(func, state);
	int32 count = menu->CountItems();
	for (int32 index = 0 ; index < count; index++) {
		BMenuItem* item = menu->ItemAt(index);
		if (item == NULL)
			continue;

		BMenu* submenu = item->Submenu();
		if (submenu != NULL)
			SetTrackingHookDeep(submenu, func, state);
	}
}


//	#pragma mark - BPopUpNavMenu


BPopUpNavMenu::BPopUpNavMenu(const char* title)
	:
	BNavMenu(title, B_REFS_RECEIVED, BMessenger(), NULL, NULL),
	fTrackThread(-1)
{
}


BPopUpNavMenu::~BPopUpNavMenu()
{
	_WaitForTrackThread();
}


void
BPopUpNavMenu::_WaitForTrackThread()
{
	if (fTrackThread >= 0) {
		status_t status;
		while (wait_for_thread(fTrackThread, &status) == B_INTERRUPTED)
			;
	}
}


void
BPopUpNavMenu::ClearMenu()
{
	RemoveItems(0, CountItems(), true);

	fMenuBuilt = false;
}


void
BPopUpNavMenu::Go(BPoint where)
{
	_WaitForTrackThread();

	fWhere = where;

	fTrackThread = spawn_thread(_TrackThread, "popup", B_DISPLAY_PRIORITY, this);
}


bool
BPopUpNavMenu::IsShowing() const
{
	return Window() != NULL && !Window()->IsHidden();
}


BPoint
BPopUpNavMenu::ScreenLocation()
{
	return fWhere;
}


int32
BPopUpNavMenu::_TrackThread(void* _menu)
{
	BPopUpNavMenu* menu = static_cast<BPopUpNavMenu*>(_menu);

	menu->Show();

	BMenuItem* result = menu->Track();
	if (result != NULL)
		static_cast<BInvoker*>(result)->Invoke();

	menu->Hide();

	return 0;
}
