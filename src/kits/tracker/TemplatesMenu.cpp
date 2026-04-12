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
 *   Distributed under the terms of the Be Sample Code License.
 */


/**
 * @file TemplatesMenu.cpp
 * @brief BMenu subclass that populates itself from the Tracker templates directory.
 *
 * TemplatesMenu reads ~/config/settings/Tracker/Tracker New Templates at
 * attachment time and builds a hierarchical menu of template files and
 * subdirectory submenus.  Items tagged with the kAttrTemplateSubMenu attribute
 * become nested submenus; plain directories become regular directory entries.
 *
 * @see BMenu, IconMenuItem, kNewEntryFromTemplate
 */


#include <Application.h>
#include <Catalog.h>
#include <Directory.h>
#include <FindDirectory.h>
#include <Locale.h>
#include <MenuItem.h>
#include <Message.h>
#include <Mime.h>
#include <NodeInfo.h>
#include <Path.h>
#include <Query.h>
#include <Roster.h>
#include <String.h>

#include <kernel/fs_attr.h>

#include "Attributes.h"
#include "Commands.h"

#include "IconMenuItem.h"
#include "MimeTypes.h"
#include "TemplatesMenu.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "TemplatesMenu"


namespace BPrivate {

const char* kTemplatesDirectory = "Tracker/Tracker New Templates";

} // namespace BPrivate


//	#pragma mark - TemplatesMenu


/**
 * @brief Construct a TemplatesMenu that will send template commands to \a target.
 *
 * @param target  Messenger to which kNewEntryFromTemplate and related
 *                messages will be dispatched.
 * @param label   Display label for the menu.
 */
TemplatesMenu::TemplatesMenu(const BMessenger& target, const char* label)
	:
	BMenu(label),
	fTarget(target),
	fOpenItem(NULL),
	fTemplateCount(0)
{
}


/**
 * @brief Destructor.
 */
TemplatesMenu::~TemplatesMenu()
{
}


/**
 * @brief Build the menu contents and set item targets when attached to a window.
 */
void
TemplatesMenu::AttachedToWindow()
{
	BuildMenu();
	BMenu::AttachedToWindow();
	SetTargetForItems(fTarget);
}


/**
 * @brief Set the target for all items in the menu and its submenus.
 *
 * Walks every top-level item, recurses into any submenus, and ensures the
 * "Edit templates" item always targets be_app_messenger.
 *
 * @param target  Handler to receive item messages.
 * @return B_OK on success, or the first error encountered.
 */
status_t
TemplatesMenu::SetTargetForItems(BHandler* target)
{
	status_t result = BMenu::SetTargetForItems(target);
	if (result != B_OK)
		return result;

	for (int i = 0; i < CountItems(); i++) {
		BMenu* submenu = ItemAt(i)->Submenu();
		if (submenu != NULL) {
			result = SetTargetForSubmenuItems(submenu, target);
			if (result != B_OK)
				return result;
		}
	}

	if (fOpenItem)
		fOpenItem->SetTarget(be_app_messenger);

	return result;
}


/**
 * @brief Set the target messenger for all items in the menu and its submenus.
 *
 * @param messenger  Messenger to receive item messages.
 * @return B_OK on success, or the first error encountered.
 */
status_t
TemplatesMenu::SetTargetForItems(BMessenger messenger)
{
	status_t result = BMenu::SetTargetForItems(messenger);
	if (result != B_OK)
		return result;

	for (int i = 0; i < CountItems(); i++) {
		BMenu* submenu = ItemAt(i)->Submenu();
		if (submenu != NULL) {
			result = SetTargetForSubmenuItems(submenu, messenger);
			if (result != B_OK)
				return result;
		}
	}

	if (fOpenItem)
		fOpenItem->SetTarget(be_app_messenger);

	return result;
}


/**
 * @brief Rebuild the menu from the templates directory.
 *
 * Clears existing items, adds the "New folder" entry, then scans the
 * user templates directory and adds an entry for each template found.
 * An "Edit templates" item is appended at the end.
 *
 * @param addItems  If true, menu items are actually added; if false, only
 *                  the item count is determined (used by UpdateMenuState).
 * @return true if at least one template item was found.
 */
bool
TemplatesMenu::BuildMenu(bool addItems)
{
	// clear everything...
	fOpenItem = NULL;
	fTemplateCount = CountItems();
	while (fTemplateCount--)
		delete RemoveItem((int32)0);

	// add the folder
	IconMenuItem* menuItem = new IconMenuItem(B_TRANSLATE("New folder"),
		new BMessage(kNewFolder), B_DIR_MIMETYPE, B_MINI_ICON);
	AddItem(menuItem);
	menuItem->SetShortcut('N', 0);
	AddSeparatorItem();

	// the templates folder
	BPath path;
	find_directory (B_USER_SETTINGS_DIRECTORY, &path, true);
	path.Append(kTemplatesDirectory);
	mkdir(path.Path(), 0777);

	fTemplateCount = 0;
	fTemplateCount += IterateTemplateDirectory(addItems, &path, this);

	// this is the message sent to open the templates folder
	BDirectory templatesDir(path.Path());
	BMessage* message = new BMessage(B_REFS_RECEIVED);
	BEntry entry;
	entry_ref dirRef;
	if (templatesDir.GetEntry(&entry) == B_OK)
		entry.GetRef(&dirRef);

	message->AddRef("refs", &dirRef);

	// add item to show templates folder
	fOpenItem = new BMenuItem(B_TRANSLATE("Edit templates" B_UTF8_ELLIPSIS), message);
	AddItem(fOpenItem);

	if (dirRef == entry_ref())
		fOpenItem->SetEnabled(false);

	return fTemplateCount > 0;
}


/**
 * @brief Create a menu item for adding a new template submenu inside \a subdirPath.
 *
 * @param subdirPath  Filesystem path of the parent templates subdirectory.
 * @return A newly allocated BMenuItem targeting kNewTemplateSubmenu.
 */
BMenuItem*
TemplatesMenu::NewSubmenuItem(BPath subdirPath)
{
	// add item to create new submenu folder
	BDirectory templatesDir(subdirPath.Path());
	BEntry entry;
	entry_ref dirRef;
	if (templatesDir.GetEntry(&entry) == B_OK)
		entry.GetRef(&dirRef);
	BMessage* message = new BMessage(kNewTemplateSubmenu);
	message->AddRef("refs", &dirRef);
	BMenuItem* submenuItem = new BMenuItem(B_TRANSLATE("Add new submenu" B_UTF8_ELLIPSIS), message);

	if (dirRef == entry_ref())
		submenuItem->SetEnabled(false);

	return submenuItem;
}


/**
 * @brief Refresh the menu state without adding items (checks for templates existence).
 */
void
TemplatesMenu::UpdateMenuState()
{
	BuildMenu(false);
}


/**
 * @brief Recursively scan a template directory and populate \a menu.
 *
 * Classifies each entry as a submenu directory (kAttrTemplateSubMenu attribute),
 * a plain sub-directory, or a file template; groups and separators are inserted
 * in that order.  Recurses for submenu directories.
 *
 * @param addItems  If false, return the count without modifying \a menu.
 * @param path      Filesystem path of the directory to scan.
 * @param menu      BMenu to populate with the found template items.
 * @return Number of template items found.
 */
int
TemplatesMenu::IterateTemplateDirectory(bool addItems, BPath* path, BMenu* menu)
{
	fTemplateCount = 0;
	if (path == NULL || menu == NULL)
		return fTemplateCount;

	BEntry entry;
	BList subMenus;
	BList subDirs;
	BList files;
	BDirectory templatesDir(path->Path());
	while (templatesDir.GetNextEntry(&entry) == B_OK) {
		BNode node(&entry);
		BNodeInfo nodeInfo(&node);
		char fileName[B_FILE_NAME_LENGTH];
		entry.GetName(fileName);
		if (nodeInfo.InitCheck() == B_OK) {
			char mimeType[B_MIME_TYPE_LENGTH];
			nodeInfo.GetType(mimeType);

			BMimeType mime(mimeType);
			if (mime.IsValid()) {
				fTemplateCount++;

				// We are just seeing if there are any items to add to the list.
				// Immediately bail and return the result.
				if (!addItems)
					return fTemplateCount;

				entry_ref ref;
				entry.GetRef(&ref);

				// Check if the template is a directory
				BDirectory dir(&entry);
				if (dir.InitCheck() == B_OK) {
					// check if the directory is a submenu, aka has kAttrTemplateSubMenu
					// (_trk/_template_submenu) attribute
					attr_info attrInfo;
					if (node.GetAttrInfo(kAttrTemplateSubMenu, &attrInfo) == B_OK) {
						ssize_t size;
						bool value;
						size = node.ReadAttr(kAttrTemplateSubMenu, B_BOOL_TYPE, 0, &value,
							sizeof(bool));
						if (size == sizeof(bool) && value == true) {
							// if submenu add it to subMenus list and iterate contents
							BPath subdirPath;
							if (entry.GetPath(&subdirPath) == B_OK) {
								BMenu* subMenu = new BMenu(fileName);
								fTemplateCount
									+= IterateTemplateDirectory(addItems, &subdirPath, subMenu);
								subMenus.AddItem((void*)subMenu);
								continue;
							}
							continue;
						}
					} else {
						// Otherwise add it to subDirs list
						BMessage* message = new BMessage(kNewEntryFromTemplate);
						message->AddRef("refs_template", &ref);
						message->AddString("name", fileName);
						subDirs.AddItem(new IconMenuItem(fileName, message, &nodeInfo,
							B_MINI_ICON));
						continue;
					}
				}

				// Add template files to files list
				BMessage* message = new BMessage(kNewEntryFromTemplate);
				message->AddRef("refs_template", &ref);
				message->AddString("name", fileName);
				files.AddItem(new IconMenuItem(fileName, message, &nodeInfo, B_MINI_ICON));
			}
		}
	}

	// Add submenus to menu
	int32 itemCount = subMenus.CountItems();
	for (int32 i = 0; i < itemCount; i++)
		menu->AddItem((BMenu*)subMenus.ItemAt(i));

	if (itemCount > 0)
		menu->AddSeparatorItem();

	// Add subdirs to menu
	itemCount = subDirs.CountItems();
	for (int32 i = 0; i < itemCount; i++)
		menu->AddItem((BMenuItem*)subDirs.ItemAt(i));

	if (itemCount > 0)
		menu->AddSeparatorItem();

	// Add files to menu
	itemCount = files.CountItems();
	for (int32 i = 0; i < itemCount; i++)
		menu->AddItem((BMenuItem*)files.ItemAt(i));

	if (itemCount > 0)
		menu->AddSeparatorItem();

	menu->AddItem(NewSubmenuItem(*path));

	return fTemplateCount > 0;
}


/**
 * @brief Recursively set the target messenger for items in \a menu and its submenus.
 *
 * @param menu       The menu whose items should be retargeted.
 * @param messenger  Messenger to receive item messages.
 * @return B_OK on success, or the first error encountered.
 */
status_t
TemplatesMenu::SetTargetForSubmenuItems(BMenu* menu, BMessenger messenger)
{
	if (!menu)
		return B_ERROR;

	status_t result;

	result = menu->SetTargetForItems(messenger);
	if (result != B_OK)
		return result;

	for (int i = 0; i < menu->CountItems(); i++) {
		BMenu* submenu = menu->ItemAt(i)->Submenu();
		if (submenu != NULL) {
			result = SetTargetForSubmenuItems(submenu, messenger);
			if (result != B_OK)
				return result;
		}
	}
	return result;
}


/**
 * @brief Recursively set the target handler for items in \a menu and its submenus.
 *
 * @param menu    The menu whose items should be retargeted.
 * @param target  Handler to receive item messages.
 * @return B_OK on success, or the first error encountered.
 */
status_t
TemplatesMenu::SetTargetForSubmenuItems(BMenu* menu, BHandler* target)
{
	if (!menu || !target)
		return B_ERROR;

	status_t result;

	result = menu->SetTargetForItems(target);
	if (result != B_OK)
		return result;

	for (int i = 0; i < menu->CountItems(); i++) {
		BMenu* submenu = menu->ItemAt(i)->Submenu();
		if (submenu != NULL) {
			result = SetTargetForSubmenuItems(submenu, target);
			if (result != B_OK)
				return result;
		}
	}
	return result;
}
