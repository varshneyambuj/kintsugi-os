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
 *   Copyright 2010 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Christophe Huriaux, c.huriaux@gmail.com
 */


/**
 * @file HttpHeaders.cpp
 * @brief Implementation of BHttpHeader and BHttpHeaders for HTTP header management.
 *
 * BHttpHeader stores a single name/value pair and lazily caches the
 * "Name: Value" raw string. BHttpHeaders owns a heap-allocated list of
 * BHttpHeader objects and provides name-based lookup, add/replace, and
 * BMessage archiving for round-tripping header sets through IPC.
 *
 * @see BHttpRequest, BHttpResult
 */


#include <ctype.h>
#include <string.h>
#include <new>

#include <String.h>
#include <HttpHeaders.h>

using namespace BPrivate::Network;


// #pragma mark -- BHttpHeader


/**
 * @brief Default constructor — creates an empty header with a valid raw cache.
 */
BHttpHeader::BHttpHeader()
	:
	fName(),
	fValue(),
	fRawHeader(),
	fRawHeaderValid(true)
{
}


/**
 * @brief Construct a BHttpHeader by parsing a raw "Name: Value" string.
 *
 * @param string  The raw header line to parse. The colon separator is
 *                required; everything before it becomes the name and
 *                everything after (trimmed) becomes the value.
 */
BHttpHeader::BHttpHeader(const char* string)
	:
	fRawHeaderValid(true)
{
	SetHeader(string);
}


/**
 * @brief Construct a BHttpHeader from separate name and value strings.
 *
 * The name is trimmed and capitalised; the value is trimmed.
 *
 * @param name   The header field name (e.g. "content-type").
 * @param value  The header field value (e.g. "text/html").
 */
BHttpHeader::BHttpHeader(const char* name, const char* value)
	:
	fRawHeaderValid(false)
{
	SetName(name);
	SetValue(value);
}


/**
 * @brief Copy constructor — copies name and value; invalidates the raw cache.
 *
 * @param copy  The source BHttpHeader to copy.
 */
BHttpHeader::BHttpHeader(const BHttpHeader& copy)
	:
	fName(copy.fName),
	fValue(copy.fValue),
	fRawHeaderValid(false)
{
}


/**
 * @brief Set the header field name, trimming and capitalising each word.
 *
 * Invalidates the cached raw header string so it is rebuilt on next access.
 *
 * @param name  The new field name.
 */
void
BHttpHeader::SetName(const char* name)
{
	fRawHeaderValid = false;
	fName = name;
	fName.Trim().CapitalizeEachWord();
}


/**
 * @brief Set the header field value, trimming surrounding whitespace.
 *
 * Invalidates the cached raw header string so it is rebuilt on next access.
 *
 * @param value  The new field value.
 */
void
BHttpHeader::SetValue(const char* value)
{
	fRawHeaderValid = false;
	fValue = value;
	fValue.Trim();
}


/**
 * @brief Parse and store a complete "Name: Value" header line.
 *
 * Splits on the first ':' character; if no colon is found, the function
 * returns false and the header remains empty.
 *
 * @param string  The raw header line to parse.
 * @return true if a colon separator was found and both parts stored,
 *         false if the string is not a valid header line.
 */
bool
BHttpHeader::SetHeader(const char* string)
{
	fRawHeaderValid = false;
	fName.Truncate(0);
	fValue.Truncate(0);

	const char* separator = strchr(string, ':');

	if (separator == NULL)
		return false;

	fName.SetTo(string, separator - string);
	fName.Trim().CapitalizeEachWord();
	SetValue(separator + 1);
	return true;
}


/**
 * @brief Return the header field name as a C string.
 *
 * @return A pointer to the null-terminated name string.
 */
const char*
BHttpHeader::Name() const
{
	return fName.String();
}


/**
 * @brief Return the header field value as a C string.
 *
 * @return A pointer to the null-terminated value string.
 */
const char*
BHttpHeader::Value() const
{
	return fValue.String();
}


/**
 * @brief Return the complete "Name: Value" header line as a C string.
 *
 * Rebuilds the raw string from fName and fValue if the cached copy is
 * stale, then returns a pointer into the internal BString buffer.
 *
 * @return A pointer to the null-terminated raw header line.
 */
const char*
BHttpHeader::Header() const
{
	if (!fRawHeaderValid) {
		fRawHeaderValid = true;

		fRawHeader.Truncate(0);
		fRawHeader << fName << ": " << fValue;
	}

	return fRawHeader.String();
}


/**
 * @brief Test whether the header field name matches \a name (case-insensitive).
 *
 * Normalises \a name by trimming and capitalising before comparing so that
 * "content-type", "Content-Type", and " Content-Type " all match.
 *
 * @param name  The name to compare against.
 * @return true if the names are equal after normalisation, false otherwise.
 */
bool
BHttpHeader::NameIs(const char* name) const
{
	return fName == BString(name).Trim().CapitalizeEachWord();
}


/**
 * @brief Assignment operator — copies name and value; invalidates the raw cache.
 *
 * @param other  The source BHttpHeader to copy.
 * @return A reference to this object.
 */
BHttpHeader&
BHttpHeader::operator=(const BHttpHeader& other)
{
	fName = other.fName;
	fValue = other.fValue;
	fRawHeaderValid = false;

	return *this;
}


// #pragma mark -- BHttpHeaders


/**
 * @brief Default constructor — creates an empty header collection.
 */
BHttpHeaders::BHttpHeaders()
	:
	fHeaderList()
{
}


/**
 * @brief Copy constructor — deep-copies all headers from \a other.
 *
 * @param other  The source BHttpHeaders collection to copy.
 */
BHttpHeaders::BHttpHeaders(const BHttpHeaders& other)
	:
	fHeaderList()
{
	*this = other;
}


/**
 * @brief Destructor — frees all heap-allocated BHttpHeader objects.
 */
BHttpHeaders::~BHttpHeaders()
{
	_EraseData();
}


// #pragma mark Header access


/**
 * @brief Look up the value of the first header with the given name.
 *
 * @param name  The header field name to search for.
 * @return A pointer to the value C string, or NULL if no match is found.
 */
const char*
BHttpHeaders::HeaderValue(const char* name) const
{
	for (int32 i = 0; i < fHeaderList.CountItems(); i++) {
		BHttpHeader* header
			= reinterpret_cast<BHttpHeader*>(fHeaderList.ItemAtFast(i));

		if (header->NameIs(name))
			return header->Value();
	}

	return NULL;
}


/**
 * @brief Return the header at the given index by reference.
 *
 * @param index  Zero-based index into the header list; must be in bounds.
 * @return A reference to the BHttpHeader at position \a index.
 */
BHttpHeader&
BHttpHeaders::HeaderAt(int32 index) const
{
	//! Note: index _must_ be in-bounds
	BHttpHeader* header
		= reinterpret_cast<BHttpHeader*>(fHeaderList.ItemAtFast(index));

	return *header;
}


// #pragma mark Header count


/**
 * @brief Return the total number of headers in the collection.
 *
 * @return The header count.
 */
int32
BHttpHeaders::CountHeaders() const
{
	return fHeaderList.CountItems();
}


// #pragma Header tests


/**
 * @brief Find the index of the first header matching \a name.
 *
 * @param name  The header field name to search for.
 * @return The zero-based index of the matching header, or -1 if not found.
 */
int32
BHttpHeaders::HasHeader(const char* name) const
{
	for (int32 i = 0; i < fHeaderList.CountItems(); i++) {
		BHttpHeader* header
			= reinterpret_cast<BHttpHeader*>(fHeaderList.ItemAt(i));

		if (header->NameIs(name))
			return i;
	}

	return -1;
}


// #pragma mark Header add/replace


/**
 * @brief Parse and add a raw "Name: Value" header line.
 *
 * @param line  The header line string to parse and store.
 * @return true if the header was successfully added, false on allocation failure.
 */
bool
BHttpHeaders::AddHeader(const char* line)
{
	return _AddOrDeleteHeader(new(std::nothrow) BHttpHeader(line));
}


/**
 * @brief Add a header with separate name and value strings.
 *
 * @param name   The header field name.
 * @param value  The header field value.
 * @return true if the header was successfully added, false on allocation failure.
 */
bool
BHttpHeaders::AddHeader(const char* name, const char* value)
{
	return _AddOrDeleteHeader(new(std::nothrow) BHttpHeader(name, value));
}


/**
 * @brief Add a header with an integer value converted to a string.
 *
 * @param name   The header field name.
 * @param value  The integer value to convert and store.
 * @return true if the header was successfully added, false on allocation failure.
 */
bool
BHttpHeaders::AddHeader(const char* name, int32 value)
{
	BString strValue;
	strValue << value;

	return AddHeader(name, strValue);
}


// #pragma mark Archiving


/**
 * @brief Populate this collection from a BMessage archive.
 *
 * Clears any existing headers and rebuilds the list from the string fields
 * stored in \a archive using each field's name as the header name.
 *
 * @param archive  The BMessage containing archived header name/value pairs.
 */
void
BHttpHeaders::PopulateFromArchive(BMessage* archive)
{
	Clear();

	int32 index = 0;
	char* nameFound;
	for (;;) {
		if (archive->GetInfo(B_STRING_TYPE, index, &nameFound, NULL) != B_OK)
			return;

		BString value = archive->FindString(nameFound);
		AddHeader(nameFound, value);

		index++;
	}
}


/**
 * @brief Store all headers into a BMessage as string name/value pairs.
 *
 * Each header's name becomes the BMessage field name and the value becomes
 * the corresponding string. This is the inverse of PopulateFromArchive().
 *
 * @param message  The BMessage to write the headers into.
 */
void
BHttpHeaders::Archive(BMessage* message) const
{
	int32 count = CountHeaders();

	for (int32 i = 0; i < count; i++) {
		BHttpHeader& header = HeaderAt(i);
		message->AddString(header.Name(), header.Value());
	}
}


// #pragma mark Header deletion


/**
 * @brief Remove all headers from the collection and free their memory.
 */
void
BHttpHeaders::Clear()
{
	_EraseData();
	fHeaderList.MakeEmpty();
}


// #pragma mark Overloaded operators


/**
 * @brief Assignment operator — replaces the collection with a copy of \a other.
 *
 * @param other  The source BHttpHeaders to copy from.
 * @return A reference to this object.
 */
BHttpHeaders&
BHttpHeaders::operator=(const BHttpHeaders& other)
{
	if (&other == this)
		return *this;

	Clear();

	for (int32 i = 0; i < other.CountHeaders(); i++)
		AddHeader(other.HeaderAt(i).Name(), other.HeaderAt(i).Value());

	return *this;
}


/**
 * @brief Return the header at \a index by reference via the subscript operator.
 *
 * @param index  Zero-based index; must be in bounds.
 * @return A reference to the BHttpHeader at position \a index.
 */
BHttpHeader&
BHttpHeaders::operator[](int32 index) const
{
	//! Note: Index _must_ be in-bounds
	BHttpHeader* header
		= reinterpret_cast<BHttpHeader*>(fHeaderList.ItemAtFast(index));

	return *header;
}


/**
 * @brief Look up a header value by name using the subscript operator.
 *
 * @param name  The header field name to search for.
 * @return A pointer to the value C string, or NULL if not found.
 */
const char*
BHttpHeaders::operator[](const char* name) const
{
	return HeaderValue(name);
}


/**
 * @brief Delete all heap-allocated BHttpHeader objects in the list.
 *
 * Does not empty fHeaderList; call Clear() for that.
 */
void
BHttpHeaders::_EraseData()
{
	// Free allocated data;
	for (int32 i = 0; i < fHeaderList.CountItems(); i++) {
		BHttpHeader* header
			= reinterpret_cast<BHttpHeader*>(fHeaderList.ItemAtFast(i));

		delete header;
	}
}


/**
 * @brief Add \a header to the list, deleting it on failure.
 *
 * @param header  A heap-allocated BHttpHeader to add; may be NULL.
 * @return true if \a header was non-NULL and successfully appended to the list,
 *         false otherwise (the object is deleted in that case).
 */
bool
BHttpHeaders::_AddOrDeleteHeader(BHttpHeader* header)
{
	if (header != NULL) {
		if (fHeaderList.AddItem(header))
			return true;
		delete header;
	}
	return false;
}
