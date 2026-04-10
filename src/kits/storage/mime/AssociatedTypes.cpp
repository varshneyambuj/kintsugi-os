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
 *   Copyright Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file AssociatedTypes.cpp
 * @brief Maps file extensions to the MIME types associated with them.
 *
 * AssociatedTypes crawls the MIME database on first use to build two internal
 * maps: one from MIME type to its registered file extensions, and one from
 * each extension to the set of MIME types that claim it.  Callers can then
 * quickly look up which MIME types are associated with a given extension, guess
 * a type for a filename, and keep both maps up to date as file-extension
 * registrations change.
 *
 * @see AssociatedTypes
 */

#include <mime/AssociatedTypes.h>

#include <stdio.h>

#include <new>

#include <Directory.h>
#include <Entry.h>
#include <Message.h>
#include <mime/database_support.h>
#include <mime/DatabaseDirectory.h>
#include <mime/DatabaseLocation.h>
#include <mime/MimeSniffer.h>
#include <MimeType.h>
#include <Path.h>
#include <String.h>
#include <storage_support.h>


#define DBG(x) x
//#define DBG(x)
#define OUT printf

namespace BPrivate {
namespace Storage {
namespace Mime {

/*!
	\class AssociatedTypes
	\brief Information about file extensions and their associated types
*/

/**
 * @brief Constructs an AssociatedTypes object.
 *
 * @param databaseLocation Pointer to the DatabaseLocation used for crawling the MIME DB.
 * @param mimeSniffer      Optional MimeSniffer used for filename-based guessing before
 *                         falling back to extension lookup; may be NULL.
 */
AssociatedTypes::AssociatedTypes(DatabaseLocation* databaseLocation,
	MimeSniffer* mimeSniffer)
	:
	fDatabaseLocation(databaseLocation),
	fMimeSniffer(mimeSniffer),
	fHaveDoneFullBuild(false)
{
}

/**
 * @brief Destroys the AssociatedTypes object.
 */
AssociatedTypes::~AssociatedTypes()
{
}

/**
 * @brief Returns a list of MIME types associated with the given file extension.
 *
 * Triggers a full database build on the first call.  Formats the extension,
 * looks it up in the internal map, and populates \a types with every MIME type
 * string that has registered the extension.
 *
 * @param extension The file extension to query (leading dots are stripped automatically).
 * @param types     Pointer to a pre-allocated BMessage that receives "types" string fields.
 * @return B_OK on success, B_BAD_VALUE if either argument is NULL or the extension is empty.
 */
status_t
AssociatedTypes::GetAssociatedTypes(const char *extension, BMessage *types)
{
	status_t err = extension && types ? B_OK : B_BAD_VALUE;
	std::string extStr;

	// See if we need to do our initial build still
	if (!err && !fHaveDoneFullBuild) {
		err = BuildAssociatedTypesTable();
	}
	// Format the extension
	if (!err) {
		extStr = PrepExtension(extension);
		err = extStr.length() > 0 ? B_OK : B_BAD_VALUE;
	}
	// Build the message
	if (!err) {
		// Clear the message, as we're just going to add to it
		types->MakeEmpty();

		// Add the types associated with this extension
		std::set<std::string> &assTypes = fAssociatedTypes[extStr];
		std::set<std::string>::const_iterator i;
		for (i = assTypes.begin(); i != assTypes.end() && !err; i++) {
			err = types->AddString(kTypesField, i->c_str());
		}
	}
	return err;
}

/**
 * @brief Guesses a MIME type for a filename based on its extension.
 *
 * Triggers a full database build on the first call.  If a MimeSniffer is
 * registered, it is consulted first; if it returns a positive priority that
 * result is used immediately.  Otherwise the filename's extension is extracted
 * and looked up in the internal map; the first matching MIME type is returned.
 *
 * @param filename The filename (or full path) to examine.
 * @param result   Pointer to a pre-allocated BString that receives the guessed type.
 * @return B_OK on success, kMimeGuessFailureError if no type could be determined,
 *         or B_BAD_VALUE if either argument is NULL.
 */
status_t
AssociatedTypes::GuessMimeType(const char *filename, BString *result)
{
	status_t err = filename && result ? B_OK : B_BAD_VALUE;
	if (!err && !fHaveDoneFullBuild)
		err = BuildAssociatedTypesTable();

	// if we have a mime sniffer, let's give it a shot first
	if (!err && fMimeSniffer != NULL) {
		BMimeType mimeType;
		float priority = fMimeSniffer->GuessMimeType(filename, &mimeType);
		if (priority >= 0) {
			*result = mimeType.Type();
			return B_OK;
		}
	}

	if (!err) {
		// Extract the extension from the file
		const char *rawExtension = strrchr(filename, '.');

		// If there was an extension, grab it and look up its associated
		// type(s). Otherwise, the best guess we can offer is
		// "application/octect-stream"
		if (rawExtension && rawExtension[1] != '\0') {
			std::string extension = PrepExtension(rawExtension + 1);

			/*! \todo I'm just grabbing the first item in the set here. Should we perhaps
				do something different?
			*/
			std::set<std::string> &types = fAssociatedTypes[extension];
			std::set<std::string>::const_iterator i = types.begin();
			if (i != types.end())
				result->SetTo(i->c_str());
			else
				err = kMimeGuessFailureError;
		} else {
			err = kMimeGuessFailureError;
		}
	}
	return err;
}

/**
 * @brief Guesses a MIME type for an entry_ref based on its filename extension.
 *
 * Converts the entry_ref to a full path and delegates to the
 * GuessMimeType(const char*, BString*) overload.
 *
 * @param ref    Pointer to the entry_ref to examine; must not be NULL.
 * @param result Pointer to a pre-allocated BString that receives the guessed type.
 * @return B_OK on success, B_BAD_VALUE if \a ref is NULL, kMimeGuessFailureError
 *         if no type could be determined, or another error code on path failure.
 */
status_t
AssociatedTypes::GuessMimeType(const entry_ref *ref, BString *result)
{
	// Convert the entry_ref to a filename and then do the check
	if (!ref)
		return B_BAD_VALUE;
	BPath path;
	status_t err = path.SetTo(ref);
	if (!err)
		err = GuessMimeType(path.Path(), result);
	return err;
}

/**
 * @brief Updates the file-extension list for a MIME type and refreshes the associated-types maps.
 *
 * Replaces the stored extension set for \a type with the extensions listed in
 * \a extensions.  Extensions that were previously registered but are absent from
 * the new list have the type removed from their associated-types set; newly added
 * extensions gain the type in their associated-types set.  If the full table has
 * not yet been built the call is a no-op (returns B_OK or B_BAD_VALUE as appropriate).
 *
 * @param type       The MIME type whose file extensions are being updated.
 * @param extensions BMessage containing the new extension strings in its
 *                   Mime::kTypesField array field.
 * @return B_OK on success, B_BAD_VALUE if either argument is NULL.
 */
status_t
AssociatedTypes::SetFileExtensions(const char *type, const BMessage *extensions)
{
	status_t err = type && extensions ? B_OK : B_BAD_VALUE;
	if (!fHaveDoneFullBuild)
		return err;

	std::set<std::string> oldExtensions;
	std::set<std::string> &newExtensions = fFileExtensions[type];
	// Make a copy of the previous extensions
	if (!err) {
		oldExtensions = newExtensions;

		// Read through the list of new extensions, creating the new
		// file extensions list and adding the type as an associated type
		// for each extension
		newExtensions.clear();
		const char *extension;
		for (int32 i = 0;
			   extensions->FindString(kTypesField, i, &extension) == B_OK;
			     i++)
		{
			newExtensions.insert(extension);
			AddAssociatedType(extension, type);
		}

		// Remove any extensions that are still associated from the list
		// of previously associated extensions
		for (std::set<std::string>::const_iterator i = newExtensions.begin();
			   i != newExtensions.end();
			     i++)
		{
			oldExtensions.erase(*i);
		}

		// Now remove the type as an associated type for any of its previously
		// but no longer associated extensions
		for (std::set<std::string>::const_iterator i = oldExtensions.begin();
			   i != oldExtensions.end();
			     i++)
		{
			RemoveAssociatedType(i->c_str(), type);
		}
	}
	return err;
}

/**
 * @brief Clears all file extensions for the given MIME type.
 *
 * Convenience wrapper that calls SetFileExtensions() with an empty BMessage,
 * effectively removing the type from every extension's associated-types set.
 *
 * @param type The MIME type whose file extensions are to be cleared.
 * @return B_OK on success, or B_BAD_VALUE if \a type is NULL.
 */
status_t
AssociatedTypes::DeleteFileExtensions(const char *type)
{
	BMessage extensions;
	return SetFileExtensions(type, &extensions);
}

/**
 * @brief Dumps the complete associated-types mapping to standard output.
 *
 * Iterates over all known extensions and prints each one together with its
 * comma-separated set of associated MIME types.  Intended for debugging only.
 */
void
AssociatedTypes::PrintToStream() const
{
	printf("\n");
	printf("-----------------\n");
	printf("Associated Types:\n");
	printf("-----------------\n");

	for (std::map<std::string, std::set<std::string> >::const_iterator i = fAssociatedTypes.begin();
		   i != fAssociatedTypes.end();
		     i++)
	{
		printf("%s: ", i->first.c_str());
		fflush(stdout);
		bool first = true;
		for (std::set<std::string>::const_iterator type = i->second.begin();
			   type != i->second.end();
			     type++)
		{
			if (first)
				first = false;
			else
				printf(", ");
			printf("%s", type->c_str());
			fflush(stdout);
		}
		printf("\n");
	}
}

/**
 * @brief Adds a MIME type to the associated-types set for the given extension.
 *
 * The extension is normalised (leading dots stripped, forced to lowercase)
 * before insertion.  Adding a type that is already present is silently ignored.
 *
 * @param extension The file extension (leading dots are stripped automatically).
 * @param type      The MIME type string to add.
 * @return B_OK on success (including when the type was already present),
 *         B_BAD_VALUE if either argument is NULL or the normalised extension is empty.
 */
status_t
AssociatedTypes::AddAssociatedType(const char *extension, const char *type)
{
	status_t err = extension && type ? B_OK : B_BAD_VALUE;
	std::string extStr;
	if (!err) {
		extStr = PrepExtension(extension);
		err = extStr.length() > 0 ? B_OK : B_BAD_VALUE;
	}
	if (!err)
		fAssociatedTypes[extStr].insert(type);
	return err;
}

/**
 * @brief Removes a MIME type from the associated-types set for the given extension.
 *
 * The extension is normalised before lookup.  Removing a type that is not
 * present is silently ignored.
 *
 * @param extension The file extension (leading dots are stripped automatically).
 * @param type      The MIME type string to remove.
 * @return B_OK on success (including when the type was not found),
 *         B_BAD_VALUE if either argument is NULL or the normalised extension is empty.
 */
status_t
AssociatedTypes::RemoveAssociatedType(const char *extension, const char *type)
{
	status_t err = extension && type ? B_OK : B_BAD_VALUE;
	std::string extStr;
	if (!err) {
		extStr = PrepExtension(extension);
		err = extStr.length() > 0 ? B_OK : B_BAD_VALUE;
	}
	if (!err)
		fAssociatedTypes[extension].erase(type);
	return err;
}

/**
 * @brief Crawls the entire MIME database and builds the extension-to-types mapping.
 *
 * Clears and rebuilds fFileExtensions and fAssociatedTypes by iterating over
 * every supertype directory and its subtypes in the MIME database location.
 * On success fHaveDoneFullBuild is set to true so that subsequent calls skip
 * this expensive operation.
 *
 * @return B_OK on success, or an error code if the database directory cannot be opened.
 */
status_t
AssociatedTypes::BuildAssociatedTypesTable()
{
	fFileExtensions.clear();
	fAssociatedTypes.clear();

	DatabaseDirectory root;
	status_t err = root.Init(fDatabaseLocation);
	if (!err) {
		root.Rewind();
		while (true) {
			BEntry entry;
			err = root.GetNextEntry(&entry);
			if (err) {
				// If we've come to the end of list, it's not an error
				if (err == B_ENTRY_NOT_FOUND)
					err = B_OK;
				break;
			} else {
				// Check that this entry is both a directory and a valid MIME string
				char supertype[B_PATH_NAME_LENGTH];
				if (entry.IsDirectory()
				      && entry.GetName(supertype) == B_OK
				         && BMimeType::IsValid(supertype))
				{
					// Make sure the supertype string is all lowercase
					BPrivate::Storage::to_lower(supertype);

					// First, iterate through this supertype directory and process
					// all of its subtypes
					DatabaseDirectory dir;
					if (dir.Init(fDatabaseLocation, supertype) == B_OK) {
						dir.Rewind();
						while (true) {
							BEntry subEntry;
							err = dir.GetNextEntry(&subEntry);
							if (err) {
								// If we've come to the end of list, it's not an error
								if (err == B_ENTRY_NOT_FOUND)
									err = B_OK;
								break;
							} else {
								// Get the subtype's name
								char subtype[B_PATH_NAME_LENGTH];
								if (subEntry.GetName(subtype) == B_OK) {
									BPrivate::Storage::to_lower(subtype);

									BString fulltype;
									fulltype.SetToFormat("%s/%s", supertype, subtype);

									// Process the subtype
									ProcessType(fulltype);
								}
							}
						}
					} else {
						DBG(OUT("Mime::AssociatedTypes::BuildAssociatedTypesTable(): "
						          "Failed opening supertype directory '%s'\n",
						            supertype));
					}

					// Second, process the supertype
					ProcessType(supertype);
				}
			}
		}
	} else {
		DBG(OUT("Mime::AssociatedTypes::BuildAssociatedTypesTable(): "
		          "Failed opening mime database directory\n"));
	}
	if (!err) {
		fHaveDoneFullBuild = true;
//		PrintToStream();
	} else {
		DBG(OUT("Mime::AssociatedTypes::BuildAssociatedTypesTable() failed, "
			"error code == 0x%" B_PRIx32 "\n", err));
	}
	return err;

}

/**
 * @brief Reads and processes the file extensions registered for a single MIME type.
 *
 * Reads the META:EXTENS attribute for \a type from the database location and
 * inserts each listed extension into fFileExtensions[type] and into the
 * fAssociatedTypes map.  Intended to be called only from BuildAssociatedTypesTable().
 *
 * @param type A valid, lowercase MIME type string (supertype or supertype/subtype).
 * @return B_OK on success, B_BAD_VALUE if \a type is NULL.
 */
status_t
AssociatedTypes::ProcessType(const char *type)
{
	status_t err = type ? B_OK : B_BAD_VALUE;
	if (!err) {
		// Read in the list of file extension types
		BMessage msg;
		if (fDatabaseLocation->ReadMessageAttribute(type, kFileExtensionsAttr,
				msg) == B_OK) {
			// Iterate through the file extesions, adding them to the list of
			// file extensions for the mime type and adding the mime type
			// to the list of associated types for each file extension
			const char *extension;
			std::set<std::string> &fileExtensions = fFileExtensions[type];
			for (int i = 0; msg.FindString(kExtensionsField, i, &extension) == B_OK; i++) {
				std::string extStr = PrepExtension(extension);
				if (extStr.length() > 0) {
					fileExtensions.insert(extStr);
					AddAssociatedType(extStr.c_str(), type);
				}
			}
		}
	}
	return err;
}

/**
 * @brief Normalises a file extension by stripping leading dots and converting to lowercase.
 *
 * @param extension The raw extension string; may begin with one or more '.' characters.
 * @return A lowercase std::string without leading dots, or an empty string if
 *         \a extension is NULL or consists solely of dots.
 */
std::string
AssociatedTypes::PrepExtension(const char *extension) const
{
	if (extension) {
		uint i = 0;
		while (extension[i] == '.')
			i++;
		return BPrivate::Storage::to_lower(&(extension[i]));
	} else {
		return "";	// This shouldn't ever happen, but if it does, an
		            // empty string is considered an invalid extension
	}
}

} // namespace Mime
} // namespace Storage
} // namespace BPrivate
