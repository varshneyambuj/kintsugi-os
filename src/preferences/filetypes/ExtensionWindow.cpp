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
 *   Copyright 2006-2010, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file ExtensionWindow.cpp
 * @brief Implementation of the modal "Extension" dialog and the helper
 *        routines that merge or replace file-name extensions on a MIME
 *        type. The on-disk extension list is stored as a "extensions"
 *        BMessage field and read or written through BMimeType.
 */


#include "ExtensionWindow.h"
#include "FileTypes.h"
#include "FileTypesWindow.h"

#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Mime.h>
#include <PopUpMenu.h>
#include <String.h>
#include <TextControl.h>

#include <strings.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Extension Window"


/** @brief Sent by the text control whenever the extension text changes. */
const uint32 kMsgExtensionUpdated = 'exup';
/** @brief Sent when the user confirms the dialog (Add / Done). */
const uint32 kMsgAccept = 'acpt';


/**
 * @brief Sort comparator for two C-string extensions.
 *
 * Performs a case-insensitive primary sort and uses a reverse case-sensitive
 * tie-breaker so that lower-case spellings appear before mixed-case ones
 * for the same letters.
 *
 * @param _a  Pointer to a const char* (qsort signature).
 * @param _b  Pointer to a const char* (qsort signature).
 * @return    Negative, zero, or positive in the usual qsort sense.
 */
static int
compare_extensions(const void* _a, const void* _b)
{
	const char* a = *(const char **)_a;
	const char* b = *(const char **)_b;

	int compare = strcasecmp(a, b);
	if (compare != 0)
		return compare;

	// sort lower case characters first
	return -strcmp(a, b);
}


/**
 * @brief Merges the entries in @a newExtensionsList into @a type's
 *        existing file-extension list, optionally dropping
 *        @a removeExtension during the merge.
 *
 * Duplicates of the new entries that already appear in the type's list
 * are filtered out, the result is sorted by compare_extensions, and the
 * combined list is written back via BMimeType::SetFileExtensions().
 *
 * @note  The strings stored in @a newExtensionsList are not copied; the
 *        caller must keep them alive until this call returns.
 *
 * @param type                Target MIME type.
 * @param newExtensionsList   BList of `const char*` entries to add.
 * @param removeExtension     Optional existing extension to drop. May be
 *                            NULL.
 * @return                    B_OK on success, or any status_t propagated
 *                            from BMimeType::GetFileExtensions /
 *                            SetFileExtensions.
 */
status_t
merge_extensions(BMimeType& type, const BList& newExtensionsList,
	const char* removeExtension)
{
	BMessage extensions;
	status_t status = type.GetFileExtensions(&extensions);
	if (status < B_OK)
		return status;

	// replace the entry, and remove any equivalent entries
	BList mergedList;
	mergedList.AddList(&newExtensionsList);
	int32 originalCount = mergedList.CountItems();

	const char* extension;
	for (int32 i = 0; extensions.FindString("extensions", i,
			&extension) == B_OK; i++) {

		for (int32 j = originalCount; j-- > 0;) {
			if (!strcmp((const char*)mergedList.ItemAt(j), extension)) {
				// Do not add this old item again, since it's already
				// there.
				mergedList.RemoveItem(j);
				originalCount--;
			}
		}

		// The item will be added behind "originalCount", so we cannot
		// remove it accidentally in the next iterations, it's is added
		// for good.
		if (removeExtension == NULL || strcmp(removeExtension, extension))
			mergedList.AddItem((void *)extension);
	}

	mergedList.SortItems(compare_extensions);

	// Copy them to a new message (their memory is still part of the
	// original BMessage)
	BMessage newExtensions;
	for (int32 i = 0; i < mergedList.CountItems(); i++) {
		newExtensions.AddString("extensions",
			(const char*)mergedList.ItemAt(i));
	}

	return type.SetFileExtensions(&newExtensions);
}


/**
 * @brief Replaces a single extension on @a type by removing
 *        @a oldExtension and inserting @a newExtension.
 *
 * Convenience wrapper over merge_extensions() for the common one-edit
 * case driven from the Extension dialog.
 *
 * @param type           Target MIME type.
 * @param newExtension   Extension to add (without leading dot).
 * @param oldExtension   Extension to remove. May be the same string as
 *                       @a newExtension when no replacement is intended.
 * @return               Result of merge_extensions().
 */
status_t
replace_extension(BMimeType& type, const char* newExtension,
	const char* oldExtension)
{
	BList list;
	list.AddItem((void *)newExtension);

	return merge_extensions(type, list, oldExtension);
}


//	#pragma mark -


/**
 * @brief Constructs the modal extension editor for a given MIME type.
 *
 * Builds a layout with a single text control and Cancel/Accept buttons,
 * filters disallowed characters from the input, strips any leading dot
 * from @a extension for display, and parents the dialog onto @a target.
 *
 * @param target     FileTypesWindow that owns this modal subset window.
 * @param type       MIME type whose extensions are being edited.
 * @param extension  Initial extension text. Empty/NULL produces an
 *                   "Add" dialog; a non-empty value produces an
 *                   "edit existing" dialog.
 */
ExtensionWindow::ExtensionWindow(FileTypesWindow* target, BMimeType& type,
		const char* extension)
	: BWindow(BRect(100, 100, 350, 200), B_TRANSLATE("Extension"),
		B_MODAL_WINDOW_LOOK, B_MODAL_SUBSET_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE
			| B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fTarget(target),
	fMimeType(type.Type()),
	fExtension(extension)
{
	fExtensionControl = new BTextControl(B_TRANSLATE("Extension:"),
		extension, NULL);
	fExtensionControl->SetModificationMessage(
		new BMessage(kMsgExtensionUpdated));
	fExtensionControl->SetAlignment(B_ALIGN_LEFT, B_ALIGN_LEFT);

	// filter out invalid characters that can't be part of an extension
	BTextView* textView = fExtensionControl->TextView();
	const char* disallowedCharacters = "/:";
	for (int32 i = 0; disallowedCharacters[i]; i++) {
		textView->DisallowChar(disallowedCharacters[i]);
	}

	fAcceptButton = new BButton(extension
		? B_TRANSLATE("Done") : B_TRANSLATE("Add"),
		new BMessage(kMsgAccept));
	fAcceptButton->SetEnabled(false);

	BButton* cancelButton = new BButton(B_TRANSLATE("Cancel"),
		new BMessage(B_QUIT_REQUESTED));

	float padding = be_control_look->DefaultItemSpacing();
	BLayoutBuilder::Grid<>(this, padding, padding)
		.SetInsets(padding, padding, padding, padding)
		.AddTextControl(fExtensionControl, 0, 0, B_ALIGN_HORIZONTAL_UNSET, 1, 2)
		.Add(BSpaceLayoutItem::CreateGlue(), 0, 1)
		.Add(cancelButton, 1, 1)
		.Add(fAcceptButton, 2, 1);

	// omit the leading dot
	if (fExtension.ByteAt(0) == '.')
		fExtension.Remove(0, 1);

	fAcceptButton->MakeDefault(true);
	fExtensionControl->MakeFocus(true);

	target->PlaceSubWindow(this);
	AddToSubset(target);
}


/**
 * @brief Destructor; layout-managed children are released by BWindow.
 */
ExtensionWindow::~ExtensionWindow()
{
}


/**
 * @brief Handles live-validation of the extension text and the final
 *        accept action that persists the change via replace_extension().
 *
 * @param message  Incoming BMessage; recognised what codes are
 *                 kMsgExtensionUpdated and kMsgAccept.
 */
void
ExtensionWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgExtensionUpdated:
		{
			bool enabled = fExtensionControl->Text() != NULL
				&& fExtensionControl->Text()[0] != '\0';
			if (enabled) {
				// There is some text, but we only accept it, if it
				// changed the previous extension
				enabled = strcmp(fExtensionControl->Text(), fExtension.String());
			}

			if (fAcceptButton->IsEnabled() != enabled)
				fAcceptButton->SetEnabled(enabled);
			break;
		}

		case kMsgAccept:
		{
			const char* newExtension = fExtensionControl->Text();
			// omit the leading dot
			if (newExtension[0] == '.')
				newExtension++;

			status_t status = replace_extension(fMimeType, newExtension,
				fExtension.String());
			if (status != B_OK)
				error_alert(B_TRANSLATE("Could not change file extensions"),
					status);

			PostMessage(B_QUIT_REQUESTED);
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}
