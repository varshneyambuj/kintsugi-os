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
 *   Copyright 2002-2006, Haiku Inc.
 *   Authors: Tyler Dauwalder, Ingo Weinhold
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file SnifferRules.cpp
 * @brief Manages and applies the set of MIME sniffer rules for the database.
 *
 * SnifferRules parses and stores the priority-ordered list of sniffer rules
 * used to identify MIME types from file contents. The rule list is built
 * lazily by scanning the database and can be queried to guess a MIME type
 * from a raw data buffer or a BFile reference.
 *
 * @see Database
 */

#include <mime/SnifferRules.h>

#include <stdio.h>
#include <sys/stat.h>

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <MimeType.h>
#include <mime/database_support.h>
#include <mime/DatabaseDirectory.h>
#include <mime/DatabaseLocation.h>
#include <mime/MimeSniffer.h>
#include <sniffer/Parser.h>
#include <sniffer/Rule.h>
#include <StorageDefs.h>
#include <storage_support.h>
#include <String.h>


#define DBG(x) x
//#define DBG(x)
#define OUT printf

namespace BPrivate {
namespace Storage {
namespace Mime {

using namespace BPrivate::Storage;

/*!
	\struct SnifferRules::sniffer_rule
	\brief A parsed sniffer rule and its corresponding mime type and rule string

	The parse sniffer rule is stored in the \c rule member, which is a pointer
	to a \c Sniffer::Rule object. This design was chosen to allow \c sniffer_rule
	objects	(as opposed to \c sniffer_rule pointers) to be used with STL objects
	without unnecessary copying. As a consequence of this decision, the
	\c SnifferRules object managing the rule list is responsible for actually
	deleting each \c sniffer_rule's \c Sniffer::Rule object.
*/

/**
 * @brief Constructs a sniffer_rule with the given parsed Sniffer::Rule pointer.
 *
 * @param rule Pointer to a heap-allocated Sniffer::Rule (ownership is NOT
 *             transferred; the SnifferRules manager is responsible for deletion).
 */
SnifferRules::sniffer_rule::sniffer_rule(Sniffer::Rule *rule)
	: rule(rule)
{
}

/**
 * @brief Destroys the sniffer_rule struct.
 *
 * The Sniffer::Rule pointed to by the rule member is NOT deleted here; that
 * responsibility belongs to the owning SnifferRules object.
 */
SnifferRules::sniffer_rule::~sniffer_rule()
{
}

/**
 * @brief Comparator that orders sniffer_rule objects by descending priority.
 *
 * Rules are placed earlier in the sorted list when they have a higher priority.
 * Rules with NULL rule pointers are treated as having minimum priority and are
 * placed at the end. Rules with identical priorities are sorted in reverse
 * alphabetical order so that subtype rules appear before their supertype rules.
 *
 * @param left  Left-hand sniffer_rule operand.
 * @param right Right-hand sniffer_rule operand.
 * @return true if @a left should appear before @a right in the sorted list.
 */
bool operator<(const SnifferRules::sniffer_rule &left, const SnifferRules::sniffer_rule &right)
{
	if (left.rule && right.rule) {
		double leftPriority = left.rule->Priority();
		double rightPriority = right.rule->Priority();
		if (leftPriority > rightPriority) {
			return true;	// left < right
		} else if (rightPriority > leftPriority) {
			return false;	// right < left
		} else {
			return left.type > right.type;
		}
	} else if (left.rule) {
		return true; 	// left < right
	} else {
		return false;	// right < left
	}
}

/*!
	\class SnifferRules
	\brief Manages the sniffer rules for the entire database
*/

/**
 * @brief Constructs a SnifferRules object.
 *
 * @param databaseLocation Pointer to the DatabaseLocation used to read rules.
 * @param mimeSniffer      Optional pointer to a MimeSniffer add-on manager.
 */
SnifferRules::SnifferRules(DatabaseLocation* databaseLocation,
	MimeSniffer* mimeSniffer)
	:
	fDatabaseLocation(databaseLocation),
	fMimeSniffer(mimeSniffer),
	fMaxBytesNeeded(0),
	fHaveDoneFullBuild(false)
{
}

/**
 * @brief Destroys the SnifferRules object and all owned Sniffer::Rule objects.
 */
SnifferRules::~SnifferRules()
{
	for (std::list<sniffer_rule>::iterator i = fRuleList.begin();
		   i != fRuleList.end(); i++) {
		delete i->rule;
		i->rule = NULL;
	}
}

/**
 * @brief Guesses the MIME type of a file by sniffing its content.
 *
 * Reads up to MaxBytesNeeded() bytes from the entry referred to by @a ref
 * and then delegates to GuessMimeType(BFile*, const void*, int32, BString*).
 *
 * @param ref  Pointer to an entry_ref identifying the file to sniff.
 * @param type Pointer to a pre-allocated BString set to the result on success.
 * @return B_OK on success, kMimeGuessFailureError if no rule matched,
 *         or another error code on failure.
 */
status_t
SnifferRules::GuessMimeType(const entry_ref *ref, BString *type)
{
	status_t err = ref && type ? B_OK : B_BAD_VALUE;
	ssize_t bytes = 0;
	char *buffer = NULL;
	BFile file;

	// First find out the max number of bytes we need to read
	// from the file to fully accomodate all of our currently
	// installed sniffer rules
	if (!err) {
		bytes = MaxBytesNeeded();
		if (bytes < 0)
			err = bytes;
	}

	// Next read that many bytes (or fewer, if the file isn't
	// that long) into a buffer
	if (!err) {
		buffer = new(std::nothrow) char[bytes];
		if (!buffer)
			err = B_NO_MEMORY;
	}

	if (!err)
		err = file.SetTo(ref, B_READ_ONLY);
	if (!err) {
		bytes = file.Read(buffer, bytes);
		if (bytes < 0)
			err = bytes;
	}

	// Now sniff the buffer
	if (!err)
		err = GuessMimeType(&file, buffer, bytes, type);

	delete[] buffer;

	return err;
}

/**
 * @brief Guesses the MIME type for a raw data buffer.
 *
 * Delegates to GuessMimeType(BFile*, const void*, int32, BString*) with a
 * NULL file argument.
 *
 * @param buffer Pointer to the data buffer to sniff.
 * @param length Number of bytes in @a buffer.
 * @param type   Pointer to a pre-allocated BString set to the result on success.
 * @return B_OK on success, kMimeGuessFailureError if no rule matched,
 *         or another error code on failure.
 */
status_t
SnifferRules::GuessMimeType(const void *buffer, int32 length, BString *type)
{
	return GuessMimeType(NULL, buffer, length, type);
}

/**
 * @brief Installs or replaces the sniffer rule for the given MIME type.
 *
 * If a rule for @a type already exists it is removed first. The new rule is
 * inserted at the correct sorted position in the priority-ordered list.
 *
 * @param type The MIME type string.
 * @param rule The sniffer rule string to parse and store.
 * @return B_OK on success, or an error code on failure.
 */
status_t
SnifferRules::SetSnifferRule(const char *type, const char *rule)
{
	status_t err = type && rule ? B_OK : B_BAD_VALUE;
	if (!err && !fHaveDoneFullBuild)
		return B_OK;

	sniffer_rule item(new Sniffer::Rule());
	BString parseError;

	// Check the mem alloc
	if (!err)
		err = item.rule ? B_OK : B_NO_MEMORY;
	// Prepare the sniffer_rule
	if (!err) {
		item.type = type;
		item.rule_string = rule;
		err = Sniffer::parse(rule, item.rule, &parseError);
		if (err)
			DBG(OUT("ERROR: SnifferRules::SetSnifferRule(): rule parsing error:\n%s\n",
				parseError.String()));
	}
	// Remove any previous rule for this type
	if (!err)
		err = DeleteSnifferRule(type);
	// Insert the new rule at the proper position in
	// the sorted rule list (remembering that our list
	// is sorted in ascending order using
	// operator<(sniffer_rule&, sniffer_rule&))
	if (!err) {
		std::list<sniffer_rule>::iterator i;
		for (i = fRuleList.begin(); i != fRuleList.end(); i++) {
			 if (item < (*i)) {
			 	fRuleList.insert(i, item);
			 	break;
			 }
		}
		if (i == fRuleList.end())
			fRuleList.push_back(item);
	}

	return err;
}

/**
 * @brief Removes the sniffer rule for the given MIME type from the rule list.
 *
 * @param type The MIME type string whose rule should be removed.
 * @return B_OK on success (even if no rule existed), or an error code.
 */
status_t
SnifferRules::DeleteSnifferRule(const char *type)
{
	status_t err = type ? B_OK : B_BAD_VALUE;
	if (!err && !fHaveDoneFullBuild)
		return B_OK;

	// Find the rule in the list and remove it
	for (std::list<sniffer_rule>::iterator i = fRuleList.begin();
		   i != fRuleList.end(); i++) {
		if (i->type == type) {
			fRuleList.erase(i);
			break;
		}
	}

	return err;
}

/**
 * @brief Prints all sniffer rules in priority-sorted order to standard output.
 */
void
SnifferRules::PrintToStream() const
{
	printf("\n");
	printf("--------------\n");
	printf("Sniffer Rules:\n");
	printf("--------------\n");

	if (fHaveDoneFullBuild) {
		for (std::list<sniffer_rule>::const_iterator i = fRuleList.begin();
			   i != fRuleList.end(); i++) {
			printf("%s: '%s'\n", i->type.c_str(), i->rule_string.c_str());
		}
	} else {
		printf("You haven't built your rule list yet, chump. ;-)\n");
	}
}

/**
 * @brief Scans the database, parses all sniffer rules, and builds the sorted list.
 *
 * The list is sorted by descending priority after all rules have been inserted.
 * The maximum bytes-needed value across all rules is computed as a side effect.
 * Sets fHaveDoneFullBuild to true on success.
 *
 * @return B_OK on success, or an error code on failure.
 */
status_t
SnifferRules::BuildRuleList()
{
	fRuleList.clear();

	ssize_t maxBytesNeeded = 0;
	ssize_t bytesNeeded = 0;
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
				         && BMimeType::IsValid(supertype)) {
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
									ProcessType(fulltype, &bytesNeeded);
									if (bytesNeeded > maxBytesNeeded)
										maxBytesNeeded = bytesNeeded;
								}
							}
						}
					} else {
						DBG(OUT("Mime::SnifferRules::BuildRuleList(): "
						          "Failed opening supertype directory '%s'\n",
						            supertype));
					}

					// Second, process the supertype
					ProcessType(supertype, &bytesNeeded);
					if (bytesNeeded > maxBytesNeeded)
						maxBytesNeeded = bytesNeeded;
				}
			}
		}
	} else {
		DBG(OUT("Mime::SnifferRules::BuildRuleList(): "
		          "Failed opening mime database directory.\n"));
	}

	if (!err) {
		fRuleList.sort();
		fMaxBytesNeeded = maxBytesNeeded;
		fHaveDoneFullBuild = true;
//		PrintToStream();
	} else {
		DBG(OUT("Mime::SnifferRules::BuildRuleList() failed, error code == 0x%"
			B_PRIx32 "\n", err));
	}
	return err;
}

/**
 * @brief Sniffs a data buffer (and optional BFile) using the installed rules.
 *
 * Searches the priority-sorted rule list for the first rule that matches
 * the given data. If a MimeSniffer add-on returns a priority at least as
 * high as the next rule to be checked, the add-on result is used instead.
 *
 * @param file   Optional BFile (may be NULL); passed to MimeSniffer add-ons.
 * @param buffer Pointer to the data buffer to sniff.
 * @param length Number of valid bytes in @a buffer.
 * @param type   Pointer to a pre-allocated BString set to the result on success.
 * @return B_OK on success, kMimeGuessFailureError if no rule matched,
 *         or another error code on failure.
 */
status_t
SnifferRules::GuessMimeType(BFile* file, const void *buffer, int32 length,
	BString *type)
{
	status_t err = buffer && type ? B_OK : B_BAD_VALUE;
	if (err)
		return err;

	// wrap the buffer by a BMemoryIO
	BMemoryIO data(buffer, length);

	if (!fHaveDoneFullBuild)
		err = BuildRuleList();

	// first ask the MIME sniffer for a suitable type
	float addonPriority = -1;
	BMimeType mimeType;
	if (!err && fMimeSniffer != NULL) {
		addonPriority = fMimeSniffer->GuessMimeType(file, buffer, length,
			&mimeType);
	}

	if (!err) {
		// Run through our rule list, which is sorted in order of
		// descreasing priority, and see if one of the rules sniffs
		// out a match
		for (std::list<sniffer_rule>::const_iterator i = fRuleList.begin();
			   i != fRuleList.end(); i++) {
			if (i->rule) {
				// If an add-on identified the type with a priority at least
				// as great as the remaining rules, we can stop further
				// processing and return the type found by the add-on.
				if (i->rule->Priority() <= addonPriority) {
					*type = mimeType.Type();
					return B_OK;
				}

				if (i->rule->Sniff(&data)) {
					type->SetTo(i->type.c_str());
					return B_OK;
				}
			} else {
				DBG(OUT("WARNING: Mime::SnifferRules::GuessMimeType(BPositionIO*,BString*): "
					"NULL sniffer_rule::rule member found in rule list for type == '%s', "
					"rule_string == '%s'\n",
					i->type.c_str(), i->rule_string.c_str()));
			}
		}

		// The sniffer add-on manager might have returned a low priority
		// (lower than any of a rule).
		if (addonPriority >= 0) {
			*type = mimeType.Type();
			return B_OK;
		}

		// If we get here, we didn't find a damn thing
		err = kMimeGuessFailureError;
	}
	return err;
}

/**
 * @brief Returns the maximum bytes any installed rule needs for a complete sniff.
 *
 * Triggers BuildRuleList() if the rule list has not yet been constructed. If a
 * MimeSniffer add-on is registered, its MinimalBufferSize() is also considered.
 *
 * @return The maximum number of bytes needed (>= 0), or a negative error code.
 */
ssize_t
SnifferRules::MaxBytesNeeded()
{
	ssize_t err = fHaveDoneFullBuild ? B_OK : BuildRuleList();
	if (!err) {
		err = fMaxBytesNeeded;

		if (fMimeSniffer != NULL) {
			fMaxBytesNeeded = max_c(fMaxBytesNeeded,
				(ssize_t)fMimeSniffer->MinimalBufferSize());
		}
	}
	return err;
}

/**
 * @brief Reads and parses the sniffer rule attribute for one MIME type.
 *
 * Called exclusively by BuildRuleList(). If the type has no sniffer rule or
 * parsing fails, a debug warning is printed but no error is propagated.
 *
 * @param type        The MIME type string (must be lowercase and valid).
 * @param bytesNeeded Returns the number of bytes this rule needs; must not be NULL.
 * @return B_OK on success, or an error code if the rule could not be processed.
 */
status_t
SnifferRules::ProcessType(const char *type, ssize_t *bytesNeeded)
{
	status_t err = type && bytesNeeded ? B_OK : B_BAD_VALUE;
	if (!err)
		*bytesNeeded = 0;

	BString str;
	BString errorMsg;
	sniffer_rule rule(new Sniffer::Rule());

	// Check the mem alloc
	if (!err)
		err = rule.rule ? B_OK : B_NO_MEMORY;
	// Read the attr
	if (!err) {
		err = fDatabaseLocation->ReadStringAttribute(type, kSnifferRuleAttr,
			str);
	}
	// Parse the rule
	if (!err) {
		err = Sniffer::parse(str.String(), rule.rule, &errorMsg);
		if (err)
			DBG(OUT("WARNING: SnifferRules::ProcessType(): Parse failure:\n%s\n", errorMsg.String()));
	}
	if (!err) {
		// Note the bytes needed
		*bytesNeeded = rule.rule->BytesNeeded();

		// Add the rule to the list
		rule.type = type;
		rule.rule_string = str.String();
		fRuleList.push_back(rule);
	}
	return err;
}

} // namespace Mime
} // namespace Storage
} // namespace BPrivate
