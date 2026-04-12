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
 * @file TrackerScripting.cpp
 * @brief BApplication scripting support for the Tracker application.
 *
 * Implements the TTracker suite that allows external applications to control
 * Tracker via the BeOS scripting protocol.  Supported operations include
 * emptying the Trash, creating folders, opening Tracker windows for specific
 * paths, and showing the preferences window.
 *
 * @see TTracker, FSEmptyTrash, FSCreateNewFolder
 */


#include <Message.h>
#include <PropertyInfo.h>

#include "ContainerWindow.h"
#include "FSUtils.h"
#include "Tracker.h"


#define kPropertyTrash "Trash"
#define kPropertyFolder "Folder"
#define kPropertyPreferences "Preferences"


/*
Available scripting commands:

doo Tracker delete Trash
doo Tracker create Folder to '/boot/home/Desktop/hello'		# mkdir
doo Tracker get Folder to '/boot/home/Desktop/hello'		# get window for path
doo Tracker execute Folder to '/boot/home/Desktop/hello'	# open window

ToDo:
Create file: on a "Tracker" "File" "B_CREATE_PROPERTY" "name"
Create query: on a "Tracker" "Query" "B_CREATE_PROPERTY" "name"
*/


const property_info kTrackerPropertyList[] = {
	{
		kPropertyTrash,
		{ B_DELETE_PROPERTY },
		{ B_DIRECT_SPECIFIER },
		"delete Trash # Empties the Trash",
		0,
		{},
		{},
		{}
	},
	{
		kPropertyFolder,
		{ B_CREATE_PROPERTY, B_GET_PROPERTY, B_EXECUTE_PROPERTY },
		{ B_DIRECT_SPECIFIER },
		"create Folder to path # creates a new folder\n"
		"get Folder to path # get Tracker window pointing to folder\n"
		"execute Folder to path # opens Tracker window pointing to folder\n",
		0,
		{ B_REF_TYPE },
		{},
		{}
	},
	{
		kPropertyPreferences,
		{ B_EXECUTE_PROPERTY },
		{ B_DIRECT_SPECIFIER },
		"shows Tracker preferences",
		0,
		{},
		{},
		{}
	},

	{ 0 }
};


/**
 * @brief Advertise the Tracker scripting suite to callers.
 *
 * Adds the kTrackerSuites identifier and the flattened property table to
 * \a data, then delegates to the base class.
 *
 * @param data  Reply message to populate with suite information.
 * @return B_OK on success, or an error code from the base class.
 */
status_t
TTracker::GetSupportedSuites(BMessage* data)
{
	data->AddString("suites", kTrackerSuites);
	BPropertyInfo propertyInfo(const_cast<property_info*>(
		kTrackerPropertyList));
	data->AddFlat("messages", &propertyInfo);

	return _inherited::GetSupportedSuites(data);
}


/**
 * @brief Resolve a scripting specifier to the appropriate handler.
 *
 * Returns @c this if the property belongs to the Tracker suite, or delegates
 * to the base class otherwise.
 *
 * @param message    The scripting message.
 * @param index      Specifier index within the message.
 * @param specifier  The specifier sub-message.
 * @param form       Specifier form constant.
 * @param property   Property name string.
 * @return The handler that should process the message.
 */
BHandler*
TTracker::ResolveSpecifier(BMessage* message, int32 index, BMessage* specifier,
	int32 form, const char* property)
{
	BPropertyInfo propertyInfo(const_cast<property_info*>(
		kTrackerPropertyList));

	int32 result = propertyInfo.FindMatch(message, index, specifier, form,
		property);
	if (result < 0) {
		//PRINT(("FindMatch result %d %s\n", result, strerror(result)));
		return _inherited::ResolveSpecifier(message, index, specifier,
			form, property);
	}

	return this;
}


/**
 * @brief Dispatch an incoming scripting message to the matching handler method.
 *
 * Extracts the specifier and property name from \a message, calls the
 * appropriate Create/Delete/Execute/Get/Set/Count helper, and sends the reply.
 *
 * @param message  The scripting message to handle.
 * @return true if the message was handled and a reply sent, false otherwise.
 */
bool
TTracker::HandleScriptingMessage(BMessage* message)
{
	if (message->what != B_GET_PROPERTY
		&& message->what != B_SET_PROPERTY
		&& message->what != B_CREATE_PROPERTY
		&& message->what != B_COUNT_PROPERTIES
		&& message->what != B_DELETE_PROPERTY
		&& message->what != B_EXECUTE_PROPERTY) {
		return false;
	}

	// dispatch scripting messages
	BMessage reply(B_REPLY);
	const char* property = NULL;
	bool handled = false;

	int32 index = 0;
	int32 form = 0;
	BMessage specifier;

	status_t result = message->GetCurrentSpecifier(&index, &specifier,
		&form, &property);
	if (result != B_OK || index == -1)
		return false;

	ASSERT(property != NULL);

	switch (message->what) {
		case B_CREATE_PROPERTY:
			handled = CreateProperty(message, &specifier, form, property,
				&reply);
			break;

		case B_GET_PROPERTY:
			handled = GetProperty(message, form, property, &reply);
			break;

		case B_SET_PROPERTY:
			handled = SetProperty(message, &specifier, form, property,
				&reply);
			break;

		case B_COUNT_PROPERTIES:
			handled = CountProperty(&specifier, form, property, &reply);
			break;

		case B_DELETE_PROPERTY:
			handled = DeleteProperty(&specifier, form, property, &reply);
			break;

		case B_EXECUTE_PROPERTY:
			handled = ExecuteProperty(message, form, property, &reply);
			break;
	}

	if (handled) {
		// done handling message, send a reply
		message->SendReply(&reply);
	}

	return handled;
}


/**
 * @brief Handle a B_CREATE_PROPERTY scripting message.
 *
 * Supports creating new folders from entry_refs supplied in \a message's
 * "data" field.
 *
 * @param message   The original scripting message.
 * @param form      Specifier form constant.
 * @param property  Name of the property to create.
 * @param reply     Reply message to populate with results or error codes.
 * @return true if the property was recognised and handled.
 */
bool
TTracker::CreateProperty(BMessage* message, BMessage*, int32 form,
	const char* property, BMessage* reply)
{
	bool handled = false;
	status_t error = B_OK;

	if (strcmp(property, kPropertyFolder) == 0) {
		if (form != B_DIRECT_SPECIFIER)
			return false;

		// create new empty folders
		entry_ref ref;
		for (int32 index = 0;
			message->FindRef("data", index, &ref) == B_OK; index++) {

			BEntry entry(&ref);
			if (!entry.Exists())
				error = FSCreateNewFolder(&ref);

			if (error != B_OK)
				break;
		}

		handled = true;
	}

	if (error != B_OK)
		reply->AddInt32("error", error);

	return handled;
}


/**
 * @brief Handle a B_DELETE_PROPERTY scripting message.
 *
 * Supports emptying the Trash via "delete Trash".
 *
 * @param form      Specifier form constant.
 * @param property  Name of the property to delete.
 * @return true if the property was recognised and handled.
 */
bool
TTracker::DeleteProperty(BMessage*, int32 form, const char* property, BMessage*)
{
	if (strcmp(property, kPropertyTrash) == 0) {
		// deleting on a selection is handled as removing a part of the
		// selection not to be confused with deleting a selected item

		if (form != B_DIRECT_SPECIFIER) {
			// only support direct specifier
			return false;
		}

		// empty the trash
		FSEmptyTrash();

		return true;

	}

	return false;
}


/**
 * @brief Handle a B_EXECUTE_PROPERTY scripting message.
 *
 * Supports "execute Preferences" to show the settings window and
 * "execute Folder" to open a Tracker window for each supplied ref.
 *
 * @param message   The original scripting message.
 * @param form      Specifier form constant.
 * @param property  Name of the property to execute.
 * @param reply     Reply message that receives opened window messengers.
 * @return true if the property was recognised and handled.
 */
bool
TTracker::ExecuteProperty(BMessage* message, int32 form, const char* property,
	BMessage* reply)
{
	if (strcmp(property, kPropertyPreferences) == 0) {
		if (form != B_DIRECT_SPECIFIER) {
			// only support direct specifier
			return false;
		}
		ShowSettingsWindow();

		return true;
	}

	if (strcmp(property, kPropertyFolder) == 0) {
		message->PrintToStream();
		if (form != B_DIRECT_SPECIFIER)
			return false;

		// create new empty folders
		entry_ref ref;
		for (int32 index = 0;
			message->FindRef("data", index, &ref) == B_OK; index++) {
			status_t error = OpenRef(&ref, NULL, NULL, kOpen, NULL);

			if (error == B_OK) {
				reply->AddMessenger("window",
					BMessenger(FindContainerWindow(&ref)));
			} else {
				reply->AddInt32("error", error);
				break;
			}
		}

		return true;
	}

	return false;
}


/**
 * @brief Handle a B_COUNT_PROPERTIES scripting message (not yet implemented).
 *
 * @return false always; counting is not supported.
 */
bool
TTracker::CountProperty(BMessage*, int32, const char*, BMessage*)
{
	return false;
}


/**
 * @brief Handle a B_GET_PROPERTY scripting message.
 *
 * Returns the messenger of the existing Tracker window for each "Folder" ref.
 *
 * @param message   The original scripting message.
 * @param form      Specifier form constant.
 * @param property  Name of the property to get.
 * @param reply     Reply message that receives window messengers or error codes.
 * @return true if the property was recognised and handled.
 */
bool
TTracker::GetProperty(BMessage* message, int32 form, const char* property,
		BMessage* reply)
{
	if (strcmp(property, kPropertyFolder) == 0) {
		message->PrintToStream();
		if (form != B_DIRECT_SPECIFIER)
			return false;

		// create new empty folders
		entry_ref ref;
		for (int32 index = 0;
			message->FindRef("data", index, &ref) == B_OK; index++) {
			BHandler* window = FindContainerWindow(&ref);

			if (window != NULL)
				reply->AddMessenger("window", BMessenger(window));
			else {
				reply->AddInt32("error", B_NAME_NOT_FOUND);
				break;
			}
		}

		return true;
	}

	return false;
}


/**
 * @brief Handle a B_SET_PROPERTY scripting message (not yet implemented).
 *
 * @return false always; setting properties is not supported.
 */
bool
TTracker::SetProperty(BMessage*, BMessage*, int32, const char*, BMessage*)
{
	return false;
}
