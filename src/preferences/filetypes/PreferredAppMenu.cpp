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
 *   Copyright 2006, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file PreferredAppMenu.cpp
 * @brief Helpers that build a "Preferred application" menu for a MIME
 *        type and resolve a user pick (a refs message dropped on a
 *        button) back to an application signature suitable for
 *        BMimeType::SetPreferredApp().
 */


#include "FileTypes.h"
#include "IconMenuItem.h"
#include "PreferredAppMenu.h"

#include <Alert.h>
#include <AppFileInfo.h>
#include <Catalog.h>
#include <Locale.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Mime.h>
#include <NodeInfo.h>
#include <String.h>

#include <stdio.h>
#include <strings.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Preferred App Menu"


/**
 * @brief Case-insensitive comparator that orders menu items by label.
 *
 * @param _a  Pointer to a BMenuItem* (qsort signature).
 * @param _b  Pointer to a BMenuItem* (qsort signature).
 * @return    Result of strcasecmp() on the two labels.
 */
static int
compare_menu_items(const void* _a, const void* _b)
{
	BMenuItem* a = *(BMenuItem**)_a;
	BMenuItem* b = *(BMenuItem**)_b;

	return strcasecmp(a->Label(), b->Label());
}


/**
 * @brief Checks whether @a app appears in the "applications" string
 *        array of @a applications.
 *
 * @param applications  Message returned by
 *                      BMimeType::GetSupportingApps() or GetWildcardApps().
 * @param app           Application signature to look for.
 * @return              True when found (case-insensitive), false otherwise.
 */
static bool
is_application_in_message(BMessage& applications, const char* app)
{
	const char* signature;
	int32 i = 0;
	while (applications.FindString("applications", i++, &signature) == B_OK) {
		if (!strcasecmp(signature, app))
			return true;
	}

	return false;
}


/**
 * @brief Disambiguates a menu item by appending its application's
 *        signature subtype to its label.
 *
 * Used when two installed applications share the same human-readable
 * short description.
 *
 * @param item       Menu item to mutate.
 * @param signature  Application signature, e.g. "application/x-vnd.Foo".
 */
static void
add_signature(BMenuItem* item, const char* signature)
{
	const char* subType = strchr(signature, '/');
	if (subType == NULL)
		return;

	char label[B_MIME_TYPE_LENGTH];
	snprintf(label, sizeof(label), "%s (%s)", item->Label(), subType + 1);

	item->SetLabel(label);
}


/**
 * @brief Builds an icon menu item for an application signature.
 *
 * The label is the application's short description when available,
 * falling back to the raw signature. The carried message has @a what
 * and a "signature" string field.
 *
 * @param signature  Application signature.
 * @param what       BMessage what code to attach.
 * @return           Newly allocated IconMenuItem; caller-owned.
 */
static IconMenuItem*
create_application_item(const char* signature, uint32 what)
{
	char name[B_FILE_NAME_LENGTH];

	BMessage* message = new BMessage(what);
	message->AddString("signature", signature);

	BMimeType applicationType(signature);
	if (applicationType.GetShortDescription(name) == B_OK)
		return new IconMenuItem(name, message, signature);

	return new IconMenuItem(signature, message, signature);
}


//	#pragma mark - Public functions


/**
 * @brief Rebuilds @a menu to reflect the preferred-app choices for
 *        @a type.
 *
 * Removes everything except the first item ("None"), adds the supporting
 * applications grouped into "supports MIME subtype" and "supports super-
 * type" sections, sorts each group by label, disambiguates colliding
 * labels with their signatures, and selects the matching entry. When the
 * preferred application is not among the supporting apps, an extra
 * separator and entry is appended so the user can still see and clear it.
 *
 * @param type            MIME type whose preferred-app list is being
 *                        edited. May be NULL to leave only "None".
 * @param menu            Pop-up menu to populate; the first item is
 *                        preserved.
 * @param what            BMessage what code attached to each generated
 *                        menu entry.
 * @param preferredFrom   When non-NULL, override which signature should
 *                        appear marked. Used to reflect a pending,
 *                        not-yet-saved selection.
 */
void
update_preferred_app_menu(BMenu* menu, BMimeType* type, uint32 what,
	const char* preferredFrom)
{
	// clear menu (but leave the first entry, ie. "None")

	for (int32 i = menu->CountItems(); i-- > 1;) {
		delete menu->RemoveItem(i);
	}

	// fill it again

	menu->ItemAt(0)->SetMarked(true);

	BMessage applications;
	if (type == NULL || type->GetSupportingApps(&applications) != B_OK)
		return;

	char preferred[B_MIME_TYPE_LENGTH];
	if (type->GetPreferredApp(preferred) != B_OK)
		preferred[0] = '\0';

	int32 lastFullSupport;
	if (applications.FindInt32("be:sub", &lastFullSupport) != B_OK)
		lastFullSupport = -1;

	BList subList;
	BList superList;

	const char* signature;
	int32 i = 0;
	while (applications.FindString("applications", i, &signature) == B_OK) {
		BMenuItem* item = create_application_item(signature, what);

		if (i < lastFullSupport)
			subList.AddItem(item);
		else
			superList.AddItem(item);

		i++;
	}

	// sort lists

	subList.SortItems(compare_menu_items);
	superList.SortItems(compare_menu_items);

	// add lists to the menu

	if (subList.CountItems() != 0 || superList.CountItems() != 0)
		menu->AddSeparatorItem();

	for (int32 i = 0; i < subList.CountItems(); i++) {
		menu->AddItem((BMenuItem*)subList.ItemAt(i));
	}

	// Add type separator
	if (superList.CountItems() != 0 && subList.CountItems() != 0)
		menu->AddSeparatorItem();

	for (int32 i = 0; i < superList.CountItems(); i++) {
		menu->AddItem((BMenuItem*)superList.ItemAt(i));
	}

	// make items unique and select current choice

	bool lastItemSame = false;
	const char* lastSignature = NULL;
	BMenuItem* last = NULL;
	BMenuItem* select = NULL;

	for (int32 index = 0; index < menu->CountItems(); index++) {
		BMenuItem* item = menu->ItemAt(index);
		if (item == NULL)
			continue;

		if (item->Message() == NULL
			|| item->Message()->FindString("signature", &signature) != B_OK)
			continue;

		if ((preferredFrom == NULL && !strcasecmp(signature, preferred))
			|| (preferredFrom != NULL
				&& !strcasecmp(signature, preferredFrom))) {
			select = item;
		}

		if (last == NULL || strcmp(last->Label(), item->Label())) {
			if (lastItemSame)
				add_signature(last, lastSignature);

			lastItemSame = false;
			last = item;
			lastSignature = signature;
			continue;
		}

		lastItemSame = true;
		add_signature(last, lastSignature);

		last = item;
		lastSignature = signature;
	}

	if (lastItemSame)
		add_signature(last, lastSignature);

	if (select != NULL) {
		// We don't select the item earlier, so that the menu field can
		// pick up the signature as well as label.
		select->SetMarked(true);
	} else if ((preferredFrom == NULL && preferred[0])
		|| (preferredFrom != NULL && preferredFrom[0])) {
		// The preferred application is not an application that support
		// this file type!
		BMenuItem* item = create_application_item(preferredFrom
			? preferredFrom : preferred, what);

		menu->AddSeparatorItem();
		menu->AddItem(item);
		item->SetMarked(true);
	}
}


/**
 * @brief Resolves a refs message dropped on a preferred-app button into
 *        the application signature to record on the MIME type.
 *
 * When @a sameAs is true, the first ref's preferred-app attribute (or
 * its MIME type's preferred app) is used. Otherwise the ref is opened
 * as an executable and its application signature is read. The function
 * also verifies that the chosen application supports @a forType and
 * displays a confirmation alert when it does not.
 *
 * @param message       Refs message; must contain at least one "refs"
 *                      entry.
 * @param sameAs        True for "use the same preferred app as this
 *                      file", false for "use this application".
 * @param forType       MIME type the preferred app will be set on.
 * @param preferredApp  Output: the resolved signature on success.
 * @retval B_OK            On success.
 * @retval B_BAD_VALUE     @a message is NULL or has no refs entry.
 * @retval B_ERROR         No usable signature could be derived, or the
 *                         user cancelled the unsupported-application
 *                         confirmation alert.
 * @retval other           Any status_t propagated from BFile, BNodeInfo,
 *                         or BAppFileInfo.
 */
status_t
retrieve_preferred_app(BMessage* message, bool sameAs, const char* forType,
	BString& preferredApp)
{
	entry_ref ref;
	if (message == NULL || message->FindRef("refs", &ref) != B_OK)
		return B_BAD_VALUE;

	BFile file(&ref, B_READ_ONLY);
	status_t status = file.InitCheck();

	char preferred[B_MIME_TYPE_LENGTH];

	if (status == B_OK) {
		if (sameAs) {
			// get preferred app from file
			BNodeInfo nodeInfo(&file);
			status = nodeInfo.InitCheck();
			if (status == B_OK) {
				if (nodeInfo.GetPreferredApp(preferred) != B_OK)
					preferred[0] = '\0';

				if (!preferred[0]) {
					// get MIME type from file
					char type[B_MIME_TYPE_LENGTH];
					if (nodeInfo.GetType(type) == B_OK) {
						BMimeType mimeType(type);
						mimeType.GetPreferredApp(preferred);
					}
				}
			}
		} else {
			// get application signature
			BAppFileInfo appInfo(&file);
			status = appInfo.InitCheck();

			if (status == B_OK && appInfo.GetSignature(preferred) != B_OK)
				preferred[0] = '\0';
		}
	}

	if (status != B_OK) {
		error_alert(B_TRANSLATE("File could not be opened"),
			status, B_STOP_ALERT);
		return status;
	}

	if (!preferred[0]) {
		error_alert(sameAs
			? B_TRANSLATE("Could not retrieve preferred application of this "
				"file")
			: B_TRANSLATE("Could not retrieve application signature"));
		return B_ERROR;
	}

	// Check if the application chosen supports this type

	BMimeType mimeType(forType);
	bool found = false;

	BMessage applications;
	if (mimeType.GetSupportingApps(&applications) == B_OK
		&& is_application_in_message(applications, preferred))
		found = true;

	applications.MakeEmpty();

	if (!found && mimeType.GetWildcardApps(&applications) == B_OK
		&& is_application_in_message(applications, preferred))
		found = true;

	if (!found) {
		// warn user
		BMimeType appType(preferred);
		char description[B_MIME_TYPE_LENGTH];
		if (appType.GetShortDescription(description) != B_OK)
			description[0] = '\0';

		char warning[512];
		snprintf(warning, sizeof(warning), B_TRANSLATE("The application "
			"\"%s\" does not support this file type.\n"
			"Are you sure you want to set it anyway?"),
			description[0] ? description : preferred);

		BAlert* alert = new BAlert(B_TRANSLATE("FileTypes request"), warning,
			B_TRANSLATE("Set preferred application"), B_TRANSLATE("Cancel"),
			NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetShortcut(1, B_ESCAPE);
		if (alert->Go() == 1)
			return B_ERROR;
	}

	preferredApp = preferred;
	return B_OK;
}

