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
 *   Copyright 2002-2010, Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT license.
 *
 *   Authors:
 *       Oliver Siebenmarck
 *       Andrew McCall, mccall@digitalparadise.co.uk
 *       Michael Wilber
 */


/**
 * @file DataTranslations.cpp
 * @brief BApplication entry point for the DataTranslations preferences app.
 *
 * Hosts the BApplication subclass that owns the DataTranslationsWindow and
 * handles drag-and-drop installation of new translator add-ons into the user
 * Translators directory by talking to the BTranslatorRoster.
 *
 * @see DataTranslationsWindow, BTranslatorRoster
 */


#include "DataTranslations.h"

#include <stdio.h>

#include <Alert.h>
#include <Catalog.h>
#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <String.h>
#include <TextView.h>
#include <TranslatorRoster.h>

#include "DataTranslationsWindow.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "DataTranslations"


/** @brief MIME signature used to register the DataTranslations preferences app. */
const char* kApplicationSignature = "application/x-vnd.Haiku-DataTranslations";


/**
 * @brief Constructs the application and creates its single top-level window.
 *
 * Registers the BApplication signature and instantiates a
 * DataTranslationsWindow which becomes the visible UI.
 */
DataTranslationsApplication::DataTranslationsApplication()
	:
	BApplication(kApplicationSignature)
{
	new DataTranslationsWindow();
}


/**
 * @brief Destructor; the framework tears down the owned window.
 */
DataTranslationsApplication::~DataTranslationsApplication()
{
}


/**
 * @brief Displays a localized error alert when installing a translator fails.
 *
 * @param name    Human-readable name of the translator entry that failed.
 * @param status  Underlying status_t error from the I/O attempt.
 */
void
DataTranslationsApplication::_InstallError(const char* name, status_t status)
{
	BString text;
	snprintf(text.LockBuffer(512), 512,
			B_TRANSLATE("Could not install %s:\n%s"), name, strerror(status));
	text.UnlockBuffer();
	BAlert* alert = new BAlert(B_TRANSLATE("DataTranslations - Error"),
		text.String(), B_TRANSLATE("OK"));
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go();
}


/**
 * @brief Installs an entry into the user Translators directory.
 *
 * Attempts to move @a entry into @a target; if a move is not possible the
 * function should fall back to copying.
 *
 * @param target  Destination directory (the user Translators folder).
 * @param entry   File system entry of the translator add-on to install.
 * @return        B_OK on success, or an error code on failure.
 * @retval B_OK     The entry was moved into @a target.
 * @retval B_ERROR  The entry could not be moved and copy fallback is
 *                  not yet implemented.
 * @todo Implement the copy fallback for cross-volume installs.
 */
status_t
DataTranslationsApplication::_Install(BDirectory& target, BEntry& entry)
{
	// Find out whether we need to copy it
	status_t status = entry.MoveTo(&target, NULL, true);
	if (status == B_OK)
		return B_OK;

	// we need to copy the file

	// TODO!
	return B_ERROR;
}


/**
 * @brief Displays an alert when a dropped file is not a valid translator.
 *
 * @param name  Display name of the rejected entry, substituted into the
 *              localized message.
 */
void
DataTranslationsApplication::_NoTranslatorError(const char* name)
{
	BString text(
		B_TRANSLATE("The item '%name' does not appear to be a Translator and "
		"will not be installed."));
	text.ReplaceAll("%name", name);
	BAlert* alert = new BAlert("", text.String(), B_TRANSLATE("OK"));
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go();
}


/**
 * @brief Handles file drops, installing each ref as a translator add-on.
 *
 * Iterates through all entry_refs in @a message, validates each as a
 * translator via BTranslatorRoster, prompts the user to overwrite when a
 * translator with the same name already exists, and finally moves the entry
 * into the user Translators directory.
 *
 * @param message  B_REFS_RECEIVED message carrying one or more "refs"
 *                 entries dragged onto the application.
 * @note On success a confirmation alert is shown; on any failure an
 *       error alert is displayed and the loop continues with the next ref.
 */
void
DataTranslationsApplication::RefsReceived(BMessage* message)
{
	BTranslatorRoster* roster = BTranslatorRoster::Default();

	BPath path;
	status_t status = find_directory(B_USER_NONPACKAGED_ADDONS_DIRECTORY,
		&path, true);
	if (status != B_OK) {
		_InstallError("translator", status);
		return;
	}

	BDirectory target;
	status = target.SetTo(path.Path());
	if (status == B_OK) {
		if (!target.Contains("Translators"))
			status = target.CreateDirectory("Translators", &target);
		else
			status = target.SetTo(&target, "Translators");
	}
	if (status != B_OK) {
		_InstallError("translator", status);
		return;
	}

	int32 i = 0;
	entry_ref ref;
	while (message->FindRef("refs", i++, &ref) == B_OK) {
		if (!roster->IsTranslator(&ref)) {
			_NoTranslatorError(ref.name);
			continue;
		}

		BEntry entry(&ref, true);
		status = entry.InitCheck();
		if (status != B_OK) {
			_InstallError(ref.name, status);
			continue;
		}

		if (target.Contains(ref.name)) {
			BString string(
				B_TRANSLATE("An item named '%name' already exists in the "
				"Translators folder! Shall the existing translator be "
				"overwritten?"));
			string.ReplaceAll("%name", ref.name);

			BAlert* alert = new BAlert(B_TRANSLATE("DataTranslations - Note"),
				string.String(), B_TRANSLATE("Cancel"),
				B_TRANSLATE("Overwrite"));
			alert->SetShortcut(0, B_ESCAPE);
			if (alert->Go() != 1)
				continue;

			// the original file will be replaced
		}

		// find out whether we need to copy it or not

		status = _Install(target, entry);
		if (status == B_OK) {
			BAlert* alert = new BAlert(B_TRANSLATE("DataTranslations - Note"),
				B_TRANSLATE("The new translator has been installed "
					"successfully."), B_TRANSLATE("OK"));
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go(NULL);
		} else
			_InstallError(ref.name, status);
	}
}


//	#pragma mark -


/**
 * @brief Program entry point; instantiates and runs the application.
 *
 * @return Process exit status (always 0 once the BApplication run loop
 *         returns).
 */
int
main(int, char**)
{
	DataTranslationsApplication app;
	app.Run();

	return 0;
}
