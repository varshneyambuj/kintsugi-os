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
 *   Copyright 2011-2013, Haiku, Inc. All rights reserved.
 *   Copyright 2011, Clemens Zeidler <haiku@clemens-zeidler.de>
 *   Copyright 2001-2003 Dr. Zoidberg Enterprises. All rights reserved.
 *
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file HaikuMailFormatFilter.cpp
 * @brief Built-in mail format filter that maps RFC 822 headers to BFS attributes.
 *
 * HaikuMailFormatFilter is inserted automatically as the first filter in every
 * BMailProtocol pipeline. During inbound processing it extracts well-known
 * header fields (To, From, Subject, Date, etc.) from the raw mail file and
 * stores them as indexed BFS attributes so that Tracker queries work correctly.
 * It also generates a canonical filename for each message and marks fully
 * fetched messages with the correct MIME type. On outbound delivery it writes
 * the "Sent" status attribute and optionally moves the file to a configured
 * outbound directory.
 *
 * @see BMailFilter, BMailProtocol, mail_util.h
 */


#include "HaikuMailFormatFilter.h"

#include <ctype.h>

#include <Directory.h>
#include <E-mail.h>
#include <NodeInfo.h>

#include <mail_util.h>


/**
 * @brief Table mapping RFC header names to BFS attribute names and types.
 *
 * Used by HeaderFetched() to extract and store header fields as BFS
 * attributes. The table is terminated by a NULL rfc_name entry.
 */
struct mail_header_field {
	const char*	rfc_name;
	const char*	attr_name;
	type_code	attr_type;
	// currently either B_STRING_TYPE and B_TIME_TYPE
};


/** @brief Default set of headers extracted and stored as BFS attributes. */
static const mail_header_field gDefaultFields[] = {
	{ "To",				B_MAIL_ATTR_TO,			B_STRING_TYPE },
	{ "From",         	B_MAIL_ATTR_FROM,		B_STRING_TYPE },
	{ "Cc",				B_MAIL_ATTR_CC,			B_STRING_TYPE },
	{ "Date",         	B_MAIL_ATTR_WHEN,		B_TIME_TYPE   },
	{ "Delivery-Date",	B_MAIL_ATTR_WHEN,		B_TIME_TYPE   },
	{ "Reply-To",     	B_MAIL_ATTR_REPLY,		B_STRING_TYPE },
	{ "Subject",      	B_MAIL_ATTR_SUBJECT,	B_STRING_TYPE },
	{ "X-Priority",		B_MAIL_ATTR_PRIORITY,	B_STRING_TYPE },
		// Priorities with preferred
	{ "Priority",		B_MAIL_ATTR_PRIORITY,	B_STRING_TYPE },
		// one first - the numeric
	{ "X-Msmail-Priority", B_MAIL_ATTR_PRIORITY, B_STRING_TYPE },
		// one (has more levels).
	{ "Mime-Version",	B_MAIL_ATTR_MIME,		B_STRING_TYPE },
	{ "STATUS",       	B_MAIL_ATTR_STATUS,		B_STRING_TYPE },
	{ "THREAD",       	B_MAIL_ATTR_THREAD,		B_STRING_TYPE },
	{ "NAME",       	B_MAIL_ATTR_NAME,		B_STRING_TYPE },
	{ NULL,				NULL,					0 }
};


/**
 * @brief Normalizes whitespace in an RFC header value string.
 *
 * Replaces all whitespace characters (tabs, newlines, etc.) with spaces and
 * collapses runs of more than one space into a single space.
 *
 * @param string  String to sanitize in-place.
 */
void
sanitize_white_space(BString& string)
{
	char* buffer = string.LockBuffer(string.Length() + 1);
	if (buffer == NULL)
		return;

	int32 count = string.Length();
	int32 spaces = 0;
	for (int32 i = 0; buffer[i] != '\0'; i++, count--) {
		if (isspace(buffer[i])) {
			buffer[i] = ' ';
			spaces++;
		} else {
			if (spaces > 1)
				memmove(buffer + i + 1 - spaces, buffer + i, count + 1);
			spaces = 0;
		}
	}

	string.UnlockBuffer();
}


// #pragma mark -


/**
 * @brief Constructs the filter and reads the configured outbound directory.
 *
 * @param protocol  The owning BMailProtocol this filter is attached to.
 * @param settings  Account settings providing account ID, name, and outbound path.
 */
HaikuMailFormatFilter::HaikuMailFormatFilter(BMailProtocol& protocol,
	const BMailAccountSettings& settings)
	:
	BMailFilter(protocol, NULL),
	fAccountID(settings.AccountID()),
	fAccountName(settings.Name())
{
	const BMessage& outboundSettings = settings.OutboundSettings();
	outboundSettings.FindString("destination", &fOutboundDirectory);
}


/**
 * @brief Returns the descriptive name of this built-in filter.
 *
 * @return The string "built-in"; never shown in the UI.
 */
BString
HaikuMailFormatFilter::DescriptiveName() const
{
	// This will not be called by the UI; no need to translate it
	return "built-in";
}


/**
 * @brief Processes a newly fetched message header and populates BFS attributes.
 *
 * Reads the header portion of the mail file, extracts the fields listed in
 * gDefaultFields, converts date fields to time_t, sanitizes strings, and
 * stores them as entries in \a attributes. Also derives a canonical filename
 * from the subject, date, and sender and stores it in \a ref so the protocol
 * can rename the file.
 *
 * @param ref         On entry, the current file ref; on exit, updated with
 *                    the canonical filename.
 * @param file        Readable BFile positioned at the start of the message.
 * @param attributes  BMessage receiving the extracted attribute key/value pairs.
 * @return B_MOVE_MAIL_ACTION to instruct the protocol to rename/move the file,
 *         or a negative error code on failure.
 */
BMailFilterAction
HaikuMailFormatFilter::HeaderFetched(entry_ref& ref, BFile& file,
	BMessage& attributes)
{
	file.Seek(0, SEEK_SET);

	// TODO: attributes.AddInt32(B_MAIL_ATTR_CONTENT, length);
	attributes.AddInt32(B_MAIL_ATTR_ACCOUNT_ID, fAccountID);
	attributes.AddString(B_MAIL_ATTR_ACCOUNT, fAccountName);

	BString header;
	off_t size;
	if (file.GetSize(&size) == B_OK) {
		char* buffer = header.LockBuffer(size);
		if (buffer == NULL)
			return B_NO_MEMORY;

		ssize_t bytesRead = file.Read(buffer, size);
		if (bytesRead < 0)
			return bytesRead;
		if (bytesRead != size)
			return B_IO_ERROR;

		header.UnlockBuffer(size);
	}

	for (int i = 0; gDefaultFields[i].rfc_name; ++i) {
		BString target;
		status_t status = extract_from_header(header,
			gDefaultFields[i].rfc_name, target);
		if (status != B_OK)
			continue;

		switch (gDefaultFields[i].attr_type){
			case B_STRING_TYPE:
				sanitize_white_space(target);
				attributes.AddString(gDefaultFields[i].attr_name, target);
				break;

			case B_TIME_TYPE:
			{
				time_t when;
				when = ParseDateWithTimeZone(target);
				if (when == -1)
					when = time(NULL); // Use current time if it's undecodable.
				attributes.AddData(B_MAIL_ATTR_WHEN, B_TIME_TYPE, &when,
					sizeof(when));
				break;
			}
		}
	}

	BString senderName = _ExtractName(attributes.FindString(B_MAIL_ATTR_FROM));
	attributes.AddString(B_MAIL_ATTR_NAME, senderName);

	// Generate a file name for the incoming message.  See also
	// Message::RenderTo which does a similar thing for outgoing messages.
	BString name = attributes.FindString(B_MAIL_ATTR_SUBJECT);
	SubjectToThread(name); // Extract the core subject words.
	if (name.Length() <= 0)
		name = "No Subject";
	attributes.AddString(B_MAIL_ATTR_THREAD, name);

	// Convert the date into a year-month-day fixed digit width format, so that
	// sorting by file name will give all the messages with the same subject in
	// order of date.
	time_t dateAsTime = 0;
	const time_t* datePntr;
	ssize_t dateSize;
	char numericDateString[40];
	struct tm timeFields;

	if (attributes.FindData(B_MAIL_ATTR_WHEN, B_TIME_TYPE,
			(const void**)&datePntr, &dateSize) == B_OK)
		dateAsTime = *datePntr;
	localtime_r(&dateAsTime, &timeFields);
	snprintf(numericDateString, sizeof(numericDateString),
		"%04d%02d%02d%02d%02d%02d",
		timeFields.tm_year + 1900, timeFields.tm_mon + 1, timeFields.tm_mday,
		timeFields.tm_hour, timeFields.tm_min, timeFields.tm_sec);
	name << " " << numericDateString;

	BString workerName = attributes.FindString(B_MAIL_ATTR_FROM);
	extract_address_name(workerName);
	name << " " << workerName;

	name.Truncate(222);	// reserve space for the unique number

	// Get rid of annoying characters which are hard to use in the shell.
	name.ReplaceAll('/', '_');
	name.ReplaceAll('\'', '_');
	name.ReplaceAll('"', '_');
	name.ReplaceAll('!', '_');
	name.ReplaceAll('<', '_');
	name.ReplaceAll('>', '_');
	_RemoveExtraWhitespace(name);
	_RemoveLeadingDots(name);
		// Avoid files starting with a dot.

	if (!attributes.HasString(B_MAIL_ATTR_STATUS))
		attributes.AddString(B_MAIL_ATTR_STATUS, "New");

	_SetType(attributes, B_PARTIAL_MAIL_TYPE);

	ref.set_name(name.String());

	return B_MOVE_MAIL_ACTION;
}


/**
 * @brief Updates the MIME type attribute when the full message body is fetched.
 *
 * Called after the complete message body has been downloaded; upgrades the
 * MIME type from B_PARTIAL_MAIL_TYPE to B_MAIL_TYPE.
 *
 * @param ref         File reference (unused here).
 * @param file        The message file (unused here).
 * @param attributes  BMessage of attributes to update.
 */
void
HaikuMailFormatFilter::BodyFetched(const entry_ref& ref, BFile& file,
	BMessage& attributes)
{
	_SetType(attributes, B_MAIL_TYPE);
}


/**
 * @brief Writes "Sent" status attributes and optionally moves the sent message.
 *
 * After a message has been successfully transmitted, this writes the
 * B_MAIL_ATTR_FLAGS and B_MAIL_ATTR_STATUS attributes and, if a destination
 * directory is configured, moves the file there.
 *
 * @param ref   entry_ref of the sent message file.
 * @param file  BFile handle for writing attributes.
 */
void
HaikuMailFormatFilter::MessageSent(const entry_ref& ref, BFile& file)
{
	mail_flags flags = B_MAIL_SENT;
	file.WriteAttr(B_MAIL_ATTR_FLAGS, B_INT32_TYPE, 0, &flags, sizeof(int32));
	file.WriteAttr(B_MAIL_ATTR_STATUS, B_STRING_TYPE, 0, "Sent", 5);

	if (!fOutboundDirectory.IsEmpty()) {
		create_directory(fOutboundDirectory, 755);
		BDirectory dir(fOutboundDirectory);
		// TODO:
//		fMailProtocol.Looper()->TriggerFileMove(ref, dir);
		BEntry entry(&ref);
		entry.MoveTo(&dir);
		// TODO: report error (via BMailProtocol::MailNotifier())
	}
}


/**
 * @brief Removes extra interior whitespace and leading/trailing spaces from a filename.
 *
 * Collapses runs of whitespace to a single space and removes any leading or
 * trailing whitespace characters.
 *
 * @param name  String to clean up in-place.
 */
void
HaikuMailFormatFilter::_RemoveExtraWhitespace(BString& name)
{
	int spaces = 0;
	for (int i = 0; i <= name.Length();) {
		if (i < name.Length() && isspace(name.ByteAt(i))) {
			spaces++;
			i++;
		} else if (spaces > 0) {
			int remove = spaces - 1;
			// Also remove leading and trailing spaces
			if (i == remove + 1 || i == name.Length())
				remove++;
			else
				name.SetByteAt(i - spaces, ' ');
			name.Remove(i - remove, remove);
			i -= remove;
			spaces = 0;
		} else
			i++;
	}
}


/**
 * @brief Removes any leading dot characters from a filename string.
 *
 * Prevents the mail daemon from creating hidden files (those starting with
 * a dot) on BFS volumes.
 *
 * @param name  String to modify in-place.
 */
void
HaikuMailFormatFilter::_RemoveLeadingDots(BString& name)
{
	int dots = 0;
	while (dots < name.Length() && name.ByteAt(dots) == '.')
		dots++;

	if (dots > 0)
		name.Remove(0, dots);
}


/**
 * @brief Extracts the display name from a From header value.
 *
 * Parses strings in the form "Name" <email@domain.com>. If no display name
 * is present, returns the email address without angle brackets. If neither
 * component is found, returns the raw header value trimmed.
 *
 * @param from  The raw From header value.
 * @return BString containing the display name or email address.
 */
BString
HaikuMailFormatFilter::_ExtractName(const BString& from)
{
	// extract name from something like "name" <email@domain.com>
	// if name is empty return the mail address without "<>"

	BString name;
	int32 emailStart = from.FindFirst("<");
	if (emailStart < 0) {
		name = from;
		return name.Trim();
	}

	from.CopyInto(name, 0, emailStart);
	name.Trim();
	if (name.Length() >= 2) {
		if (name[name.Length() - 1] == '\"')
			name.Truncate(name.Length() - 1, true);
		if (name[0] == '\"')
			name.Remove(0, 1);
		name.Trim();
	}
	if (name != "")
		return name;

	// empty name extract email address
	name = from;
	name.Remove(0, emailStart + 1);
	name.Trim();
	if (name.Length() < 1)
		return from;
	if (name[name.Length() - 1] == '>')
		name.Truncate(name.Length() - 1, true);
	name.Trim();
	return name;
}


/**
 * @brief Stores a MIME type string in the "BEOS:TYPE" attribute field.
 *
 * @param attributes  BMessage to update.
 * @param mimeType    Null-terminated MIME type string to store.
 * @return B_OK on success, or a BMessage error code on failure.
 */
status_t
HaikuMailFormatFilter::_SetType(BMessage& attributes, const char* mimeType)
{
	return attributes.SetData("BEOS:TYPE", B_MIME_STRING_TYPE, mimeType,
		strlen(mimeType) + 1, false);
}
