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
 *   Copyright 2010-2014 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Christophe Huriaux, c.huriaux@gmail.com
 *       Hamish Morrison, hamishm53@gmail.com
 */


/**
 * @file NetworkCookieJar.cpp
 * @brief Implementation of BNetworkCookieJar, the thread-safe HTTP cookie store.
 *
 * Stores cookies in a domain-keyed hash map where each bucket is a
 * reader-writer-locked list of BNetworkCookie objects sorted by descending
 * path length. Supports BArchivable serialisation, Netscape cookie-file
 * flat-text format via BFlattenable, and two iterator types: a full flat
 * iterator and a URL-scoped iterator that walks domain suffixes.
 *
 * @see BNetworkCookie, BHttpRequest
 */


#include <new>
#include <stdio.h>

#include <HashMap.h>
#include <HashString.h>
#include <Message.h>
#include <NetworkCookieJar.h>

#include "NetworkCookieJarPrivate.h"

using namespace BPrivate::Network;


// #define TRACE_COOKIE
#ifdef TRACE_COOKIE
#	define TRACE(x...) printf(x)
#else
#	define TRACE(x...) ;
#endif


const char* kArchivedCookieMessageName = "be:cookie";


/**
 * @brief Default constructor — creates an empty cookie jar.
 */
BNetworkCookieJar::BNetworkCookieJar()
	:
	fCookieHashMap(new(std::nothrow) PrivateHashMap())
{
}


/**
 * @brief Copy constructor — deep-copies all cookies from \a other.
 *
 * @param other  The source BNetworkCookieJar to copy.
 */
BNetworkCookieJar::BNetworkCookieJar(const BNetworkCookieJar& other)
	:
	fCookieHashMap(new(std::nothrow) PrivateHashMap())
{
	*this = other;
}


/**
 * @brief Construct a cookie jar pre-populated from a list of cookies.
 *
 * @param otherList  A BNetworkCookieList whose cookies are added individually.
 */
BNetworkCookieJar::BNetworkCookieJar(const BNetworkCookieList& otherList)
	:
	fCookieHashMap(new(std::nothrow) PrivateHashMap())
{
	AddCookies(otherList);
}


/**
 * @brief Construct a cookie jar by restoring cookies from a BMessage archive.
 *
 * Iterates over all "be:cookie" sub-messages and adds each as a new
 * BNetworkCookie. Cookies that fail to add (invalid or duplicate) are
 * silently discarded.
 *
 * @param archive  A BMessage previously produced by Archive().
 */
BNetworkCookieJar::BNetworkCookieJar(BMessage* archive)
	:
	fCookieHashMap(new(std::nothrow) PrivateHashMap())
{
	BMessage extractedCookie;

	for (int32 i = 0; archive->FindMessage(kArchivedCookieMessageName, i,
			&extractedCookie) == B_OK; i++) {
		BNetworkCookie* heapCookie
			= new(std::nothrow) BNetworkCookie(&extractedCookie);

		if (heapCookie == NULL)
			break;

		if (AddCookie(heapCookie) != B_OK) {
			delete heapCookie;
			continue;
		}
	}
}


/**
 * @brief Destructor — deletes all cookies and frees the hash map.
 *
 * Iterates the jar and deletes each cookie, then iterates the hash map
 * to delete each per-domain list before deleting the map itself.
 */
BNetworkCookieJar::~BNetworkCookieJar()
{
	for (Iterator it = GetIterator(); it.Next() != NULL;)
		delete it.Remove();

	fCookieHashMap->Lock();

	PrivateHashMap::Iterator it = fCookieHashMap->GetIterator();
	while (it.HasNext()) {
		BNetworkCookieList* list = it.Next().value;
		list->LockForWriting();
		delete list;
	}

	delete fCookieHashMap;
}


// #pragma mark Add cookie to cookie jar


/**
 * @brief Add a cookie to the jar by copying it.
 *
 * Creates a heap-allocated copy of \a cookie and delegates to the
 * pointer-taking overload. Deletes the copy if insertion fails.
 *
 * @param cookie  The BNetworkCookie to copy and add.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or an error
 *         code from the pointer overload if the cookie is invalid.
 */
status_t
BNetworkCookieJar::AddCookie(const BNetworkCookie& cookie)
{
	BNetworkCookie* heapCookie = new(std::nothrow) BNetworkCookie(cookie);
	if (heapCookie == NULL)
		return B_NO_MEMORY;

	status_t result = AddCookie(heapCookie);
	if (result != B_OK)
		delete heapCookie;

	return result;
}


/**
 * @brief Parse a Set-Cookie string and add the resulting cookie.
 *
 * Creates a BNetworkCookie from \a cookie and \a referrer, then delegates
 * to the pointer-taking overload.
 *
 * @param cookie    The Set-Cookie header value string.
 * @param referrer  The URL from which the Set-Cookie header was received.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or a parse
 *         error code if the cookie string is invalid.
 */
status_t
BNetworkCookieJar::AddCookie(const BString& cookie, const BUrl& referrer)
{
	BNetworkCookie* heapCookie = new(std::nothrow) BNetworkCookie(cookie,
		referrer);

	if (heapCookie == NULL)
		return B_NO_MEMORY;

	status_t result = AddCookie(heapCookie);

	if (result != B_OK)
		delete heapCookie;

	return result;
}


/**
 * @brief Add a heap-allocated cookie to the jar, taking ownership.
 *
 * Locates or creates the per-domain list for the cookie, removes any
 * existing cookie with the same name and path (update semantics), and inserts
 * the new cookie in descending path-length order. If the cookie's expiration
 * is in the past, it is deleted (effectively removing the existing entry).
 *
 * The cookie's raw string cache is pre-populated before insertion so that
 * subsequent multi-threaded reads require no locking.
 *
 * @param cookie  A heap-allocated BNetworkCookie. The jar takes ownership.
 * @return B_OK on success, B_NO_MEMORY if the hash map is NULL or a list
 *         cannot be allocated, B_BAD_VALUE if the cookie is NULL or invalid,
 *         or B_ERROR on locking failure.
 */
status_t
BNetworkCookieJar::AddCookie(BNetworkCookie* cookie)
{
	if (fCookieHashMap == NULL)
		return B_NO_MEMORY;

	if (cookie == NULL || !cookie->IsValid())
		return B_BAD_VALUE;

	HashString key(cookie->Domain());

	if (!fCookieHashMap->Lock())
		return B_ERROR;

	// Get the cookies for the requested domain, or create a new list if there
	// isn't one yet.
	BNetworkCookieList* list = fCookieHashMap->Get(key);
	if (list == NULL) {
		list = new(std::nothrow) BNetworkCookieList();

		if (list == NULL) {
			fCookieHashMap->Unlock();
			return B_NO_MEMORY;
		}

		if (fCookieHashMap->Put(key, list) != B_OK) {
			fCookieHashMap->Unlock();
			delete list;
			return B_NO_MEMORY;
		}
	}

	if (list->LockForWriting() != B_OK) {
		fCookieHashMap->Unlock();
		return B_ERROR;
	}

	fCookieHashMap->Unlock();

	// Remove any cookie with the same key as the one we're trying to add (it
	// replaces/updates them)
	for (int32 i = 0; i < list->CountItems(); i++) {
		const BNetworkCookie* c = list->ItemAt(i);

		if (c->Name() == cookie->Name() && c->Path() == cookie->Path()) {
			list->RemoveItemAt(i);
			break;
		}
	}

	// If the cookie has an expiration date in the past, stop here: we
	// effectively deleted a cookie.
	if (cookie->ShouldDeleteNow()) {
		TRACE("Remove cookie: %s\n", cookie->RawCookie(true).String());
		delete cookie;
	} else {
		// Make sure the cookie has cached the raw string and expiration date
		// string, so it is now actually immutable. This way we can safely
		// read the cookie data from multiple threads without any locking.
		const BString& raw = cookie->RawCookie(true);
		(void)raw;

		TRACE("Add cookie: %s\n", raw.String());

		// Keep the list sorted by path length (longest first). This makes sure
		// that cookies for most specific paths are returned first when
		// iterating the cookie jar.
		int32 i;
		for (i = 0; i < list->CountItems(); i++) {
			const BNetworkCookie* current = list->ItemAt(i);
			if (current->Path().Length() < cookie->Path().Length())
				break;
		}
		list->AddItem(cookie, i);
	}

	list->Unlock();

	return B_OK;
}


/**
 * @brief Add all cookies from a BNetworkCookieList by reference-copying each.
 *
 * @param cookies  The list of cookies to add.
 * @return B_OK if all cookies were added, or the first error code encountered.
 */
status_t
BNetworkCookieJar::AddCookies(const BNetworkCookieList& cookies)
{
	for (int32 i = 0; i < cookies.CountItems(); i++) {
		const BNetworkCookie* cookiePtr = cookies.ItemAt(i);

		// Using AddCookie by reference in order to avoid multiple
		// cookie jar share the same cookie pointers
		status_t result = AddCookie(*cookiePtr);
		if (result != B_OK)
			return result;
	}

	return B_OK;
}


// #pragma mark Purge useless cookies


/**
 * @brief Remove and delete all cookies whose expiration date has passed.
 *
 * @return The number of cookies deleted.
 */
uint32
BNetworkCookieJar::DeleteOutdatedCookies()
{
	int32 deleteCount = 0;
	const BNetworkCookie* cookiePtr;

	for (Iterator it = GetIterator(); (cookiePtr = it.Next()) != NULL;) {
		if (cookiePtr->ShouldDeleteNow()) {
			delete it.Remove();
			deleteCount++;
		}
	}

	return deleteCount;
}


/**
 * @brief Remove and delete all session cookies and expired cookies.
 *
 * Should be called when the application is about to exit to enforce the
 * semantics of session cookies.
 *
 * @return The number of cookies deleted.
 */
uint32
BNetworkCookieJar::PurgeForExit()
{
	int32 deleteCount = 0;
	const BNetworkCookie* cookiePtr;

	for (Iterator it = GetIterator(); (cookiePtr = it.Next()) != NULL;) {
		if (cookiePtr->ShouldDeleteAtExit()) {
			delete it.Remove();
			deleteCount++;
		}
	}

	return deleteCount;
}


// #pragma mark BArchivable interface


/**
 * @brief Archive all cookies in the jar to a BMessage.
 *
 * Each cookie is archived as a sub-message with key "be:cookie".
 *
 * @param into  The BMessage to archive into.
 * @param deep  Passed through to BArchivable::Archive().
 * @return B_OK on success, or the first error returned by cookie archiving.
 */
status_t
BNetworkCookieJar::Archive(BMessage* into, bool deep) const
{
	status_t error = BArchivable::Archive(into, deep);

	if (error == B_OK) {
		const BNetworkCookie* cookiePtr;

		for (Iterator it = GetIterator(); (cookiePtr = it.Next()) != NULL;) {
			BMessage subArchive;

			error = cookiePtr->Archive(&subArchive, deep);
			if (error != B_OK)
				return error;

			error = into->AddMessage(kArchivedCookieMessageName, &subArchive);
			if (error != B_OK)
				return error;
		}
	}

	return error;
}


/**
 * @brief Instantiate a BNetworkCookieJar from a BMessage archive.
 *
 * @param archive  A BMessage previously produced by Archive().
 * @return A new heap-allocated BNetworkCookieJar, or NULL on failure.
 */
BArchivable*
BNetworkCookieJar::Instantiate(BMessage* archive)
{
	if (archive->HasMessage(kArchivedCookieMessageName))
		return new(std::nothrow) BNetworkCookieJar(archive);

	return NULL;
}


// #pragma mark BFlattenable interface


/**
 * @brief Return false because the flattened size varies.
 *
 * @return false always.
 */
bool
BNetworkCookieJar::IsFixedSize() const
{
	// Flattened size vary
	return false;
}


/**
 * @brief Return the type code used for flattened cookie jar data.
 *
 * @return B_ANY_TYPE (a dedicated type constant is not yet defined).
 */
type_code
BNetworkCookieJar::TypeCode() const
{
	// TODO: Add a B_COOKIEJAR_TYPE
	return B_ANY_TYPE;
}


/**
 * @brief Return the byte size of the flattened cookie jar representation.
 *
 * Forces regeneration of the flattened string via _DoFlatten() and returns
 * its length plus one for the null terminator.
 *
 * @return The number of bytes required by Flatten().
 */
ssize_t
BNetworkCookieJar::FlattenedSize() const
{
	_DoFlatten();
	return fFlattened.Length() + 1;
}


/**
 * @brief Write the flattened cookie jar to a buffer in Netscape cookie format.
 *
 * The flat format is tab-separated fields per line: domain, TRUE, path,
 * secure flag, expiration timestamp, name, value.
 *
 * @param buffer  Output buffer to write into; must be at least FlattenedSize() bytes.
 * @param size    Size of \a buffer in bytes.
 * @return B_OK on success, or B_ERROR if \a size is too small.
 */
status_t
BNetworkCookieJar::Flatten(void* buffer, ssize_t size) const
{
	if (FlattenedSize() > size)
		return B_ERROR;

	fFlattened.CopyInto(reinterpret_cast<char*>(buffer), 0,
		fFlattened.Length());
	reinterpret_cast<char*>(buffer)[fFlattened.Length()] = 0;

	return B_OK;
}


/**
 * @brief Return false because AllowsTypeCode() is not yet implemented.
 *
 * @return false always.
 */
bool
BNetworkCookieJar::AllowsTypeCode(type_code) const
{
	// TODO
	return false;
}


/**
 * @brief Populate the jar from a Netscape-format flat cookie buffer.
 *
 * Parses newline-separated records, each with seven tab-separated fields:
 * domain, subdomain flag, path, secure flag, expiration timestamp, name,
 * value. Lines beginning with '#' are treated as comments.
 *
 * @param  (unused type_code)  The type code of the flattened data.
 * @param buffer  The flat buffer to parse.
 * @param size    Number of bytes in \a buffer.
 * @return B_OK always (parse errors on individual cookies are silently ignored).
 */
status_t
BNetworkCookieJar::Unflatten(type_code, const void* buffer, ssize_t size)
{
	BString flattenedCookies;
	flattenedCookies.SetTo(reinterpret_cast<const char*>(buffer), size);

	while (flattenedCookies.Length() > 0) {
		BNetworkCookie tempCookie;
		BString tempCookieLine;

		int32 endOfLine = flattenedCookies.FindFirst('\n', 0);
		if (endOfLine == -1)
			tempCookieLine = flattenedCookies;
		else {
			flattenedCookies.MoveInto(tempCookieLine, 0, endOfLine);
			flattenedCookies.Remove(0, 1);
		}

		if (tempCookieLine.Length() != 0 && tempCookieLine[0] != '#') {
			for (int32 field = 0; field < 7; field++) {
				BString tempString;

				int32 endOfField = tempCookieLine.FindFirst('\t', 0);
				if (endOfField == -1)
					tempString = tempCookieLine;
				else {
					tempCookieLine.MoveInto(tempString, 0, endOfField);
					tempCookieLine.Remove(0, 1);
				}

				switch (field) {
					case 0:
						tempCookie.SetDomain(tempString);
						break;

					case 1:
						// TODO: Useless field ATM
						break;

					case 2:
						tempCookie.SetPath(tempString);
						break;

					case 3:
						tempCookie.SetSecure(tempString == "TRUE");
						break;

					case 4:
						tempCookie.SetExpirationDate(atoi(tempString));
						break;

					case 5:
						tempCookie.SetName(tempString);
						break;

					case 6:
						tempCookie.SetValue(tempString);
						break;
				} // switch
			} // for loop

			AddCookie(tempCookie);
		}
	}

	return B_OK;
}


/**
 * @brief Assignment operator — replaces all cookies with those from \a other.
 *
 * Deletes all existing cookies, then iterates \a other and adds copies.
 *
 * @param other  The source BNetworkCookieJar to copy from.
 * @return A reference to this object.
 */
BNetworkCookieJar&
BNetworkCookieJar::operator=(const BNetworkCookieJar& other)
{
	if (&other == this)
		return *this;

	for (Iterator it = GetIterator(); it.Next() != NULL;)
		delete it.Remove();

	BArchivable::operator=(other);
	BFlattenable::operator=(other);

	fFlattened = other.fFlattened;

	delete fCookieHashMap;
	fCookieHashMap = new(std::nothrow) PrivateHashMap();

	for (Iterator it = other.GetIterator(); it.HasNext();) {
		const BNetworkCookie* cookie = it.Next();
		AddCookie(*cookie); // Pass by reference so the cookie is copied.
	}

	return *this;
}


// #pragma mark Iterators


/**
 * @brief Return a flat iterator positioned at the first cookie in the jar.
 *
 * @return A BNetworkCookieJar::Iterator for traversing all cookies.
 */
BNetworkCookieJar::Iterator
BNetworkCookieJar::GetIterator() const
{
	return BNetworkCookieJar::Iterator(this);
}


/**
 * @brief Return a URL-scoped iterator for cookies applicable to \a url.
 *
 * If the URL has no path, a path of "/" is assumed so that the iterator
 * can correctly match cookies.
 *
 * @param url  The URL to match cookies against.
 * @return A BNetworkCookieJar::UrlIterator scoped to \a url.
 */
BNetworkCookieJar::UrlIterator
BNetworkCookieJar::GetUrlIterator(const BUrl& url) const
{
	if (!url.HasPath()) {
		BUrl copy(url);
		copy.SetPath("/");
		return BNetworkCookieJar::UrlIterator(this, copy);
	}

	return BNetworkCookieJar::UrlIterator(this, url);
}


/**
 * @brief Rebuild the Netscape-format flat cookie string from the current jar.
 *
 * Iterates all cookies and writes each as a tab-separated line into fFlattened.
 */
void
BNetworkCookieJar::_DoFlatten() const
{
	fFlattened.Truncate(0);

	const BNetworkCookie* cookiePtr;
	for (Iterator it = GetIterator(); (cookiePtr = it.Next()) != NULL;) {
		fFlattened 	<< cookiePtr->Domain() << '\t' << "TRUE" << '\t'
			<< cookiePtr->Path() << '\t'
			<< (cookiePtr->Secure()?"TRUE":"FALSE") << '\t'
			<< (int32)cookiePtr->ExpirationDate() << '\t'
			<< cookiePtr->Name() << '\t' << cookiePtr->Value() << '\n';
	}
}


// #pragma mark Iterator


/**
 * @brief Copy constructor — creates an independent iterator at the same position.
 *
 * @param other  The source iterator to copy.
 */
BNetworkCookieJar::Iterator::Iterator(const Iterator& other)
	:
	fCookieJar(other.fCookieJar),
	fIterator(NULL),
	fLastList(NULL),
	fList(NULL),
	fElement(NULL),
	fLastElement(NULL),
	fIndex(0)
{
	fIterator = new(std::nothrow) PrivateIterator(
		fCookieJar->fCookieHashMap->GetIterator());

	_FindNext();
}


/**
 * @brief Construct an iterator over all cookies in \a cookieJar.
 *
 * @param cookieJar  The jar to iterate over.
 */
BNetworkCookieJar::Iterator::Iterator(const BNetworkCookieJar* cookieJar)
	:
	fCookieJar(const_cast<BNetworkCookieJar*>(cookieJar)),
	fIterator(NULL),
	fLastList(NULL),
	fList(NULL),
	fElement(NULL),
	fLastElement(NULL),
	fIndex(0)
{
	fIterator = new(std::nothrow) PrivateIterator(
		fCookieJar->fCookieHashMap->GetIterator());

	// Locate first cookie
	_FindNext();
}


/**
 * @brief Destructor — releases all read locks and deletes the private iterator.
 */
BNetworkCookieJar::Iterator::~Iterator()
{
	if (fList != NULL)
		fList->Unlock();
	if (fLastList != NULL)
		fLastList->Unlock();

	delete fIterator;
}


/**
 * @brief Assignment operator — resets and repositions this iterator.
 *
 * @param other  The source iterator to copy.
 * @return A reference to this iterator.
 */
BNetworkCookieJar::Iterator&
BNetworkCookieJar::Iterator::operator=(const Iterator& other)
{
	if (this == &other)
		return *this;

	delete fIterator;
	if (fList != NULL)
		fList->Unlock();

	fCookieJar = other.fCookieJar;
	fIterator = NULL;
	fLastList = NULL;
	fList = NULL;
	fElement = NULL;
	fLastElement = NULL;
	fIndex = 0;

	fIterator = new(std::nothrow) PrivateIterator(
		fCookieJar->fCookieHashMap->GetIterator());

	_FindNext();

	return *this;
}


/**
 * @brief Return whether more cookies remain in the jar.
 *
 * @return true if Next() will return a valid cookie pointer.
 */
bool
BNetworkCookieJar::Iterator::HasNext() const
{
	return fElement;
}


/**
 * @brief Advance the iterator and return the current cookie.
 *
 * @return A const pointer to the current BNetworkCookie, or NULL at end.
 */
const BNetworkCookie*
BNetworkCookieJar::Iterator::Next()
{
	if (!fElement)
		return NULL;

	const BNetworkCookie* result = fElement;
	_FindNext();
	return result;
}


/**
 * @brief Advance to the first cookie of the next domain bucket.
 *
 * Skips remaining cookies in the current domain list and returns the first
 * cookie of the next domain (or NULL if no further domains exist).
 *
 * @return A const pointer to the first cookie of the next domain, or NULL.
 */
const BNetworkCookie*
BNetworkCookieJar::Iterator::NextDomain()
{
	if (!fElement)
		return NULL;

	const BNetworkCookie* result = fElement;

	if (!fIterator->fCookieMapIterator.HasNext()) {
		fElement = NULL;
		return result;
	}

	if (fList != NULL)
		fList->Unlock();

	if (fCookieJar->fCookieHashMap->Lock()) {
		fList = fIterator->fCookieMapIterator.Next().value;
		fList->LockForReading();

		while (fList->CountItems() == 0
			&& fIterator->fCookieMapIterator.HasNext()) {
			// Empty list. Skip it
			fList->Unlock();
			fList = fIterator->fCookieMapIterator.Next().value;
			fList->LockForReading();
		}

		fCookieJar->fCookieHashMap->Unlock();
	}

	fIndex = 0;
	fElement = fList->ItemAt(fIndex);
	return result;
}


/**
 * @brief Remove the last cookie returned by Next() from the jar.
 *
 * Handles boundary cases where the last element was in the previous domain
 * list (fLastList) rather than the current list.
 *
 * @return A const pointer to the removed cookie (caller takes ownership), or
 *         NULL if no cookie has been returned yet.
 */
const BNetworkCookie*
BNetworkCookieJar::Iterator::Remove()
{
	if (!fLastElement)
		return NULL;

	const BNetworkCookie* result = fLastElement;

	if (fIndex == 0) {
		if (fLastList && fCookieJar->fCookieHashMap->Lock()) {
			// We are on the first item of fList, so we need to remove the
			// last of fLastList
			fLastList->Unlock();
			if (fLastList->LockForWriting() == B_OK) {
				fLastList->RemoveItemAt(fLastList->CountItems() - 1);
				// TODO if the list became empty, we could remove it from the
				// map, but this can be a problem if other iterators are still
				// referencing it. Is there a safe place and locking pattern
				// where we can do that?
				fLastList->Unlock();
				fLastList->LockForReading();
			}
			fCookieJar->fCookieHashMap->Unlock();
		}
	} else {
		fIndex--;

		if (fCookieJar->fCookieHashMap->Lock()) {
			// Switch to a write lock
			fList->Unlock();
			if (fList->LockForWriting() == B_OK) {
				fList->RemoveItemAt(fIndex);
				fList->Unlock();
			}
			fList->LockForReading();
			fCookieJar->fCookieHashMap->Unlock();
		}
	}

	fLastElement = NULL;
	return result;
}


/**
 * @brief Advance fElement to the next cookie in the flat iteration order.
 *
 * Moves within the current domain list, or advances to the next non-empty
 * domain list when the current one is exhausted. Acquires and releases
 * reader locks as needed.
 */
void
BNetworkCookieJar::Iterator::_FindNext()
{
	fLastElement = fElement;

	fIndex++;
	if (fList && fIndex < fList->CountItems()) {
		// Get an element from the current list
		fElement = fList->ItemAt(fIndex);
		return;
	}

	if (fIterator == NULL || !fIterator->fCookieMapIterator.HasNext()) {
		// We are done iterating
		fElement = NULL;
		return;
	}

	// Get an element from the next list
	if (fLastList != NULL) {
		fLastList->Unlock();
	}
	fLastList = fList;

	if (fCookieJar->fCookieHashMap->Lock()) {
		fList = (fIterator->fCookieMapIterator.Next().value);
		fList->LockForReading();

		while (fList->CountItems() == 0
			&& fIterator->fCookieMapIterator.HasNext()) {
			// Empty list. Skip it
			fList->Unlock();
			fList = fIterator->fCookieMapIterator.Next().value;
			fList->LockForReading();
		}

		fCookieJar->fCookieHashMap->Unlock();
	}

	fIndex = 0;
	fElement = fList->ItemAt(fIndex);
}


// #pragma mark URL Iterator


/**
 * @brief Copy constructor — creates an independent URL iterator at the same position.
 *
 * @param other  The source UrlIterator to copy.
 */
BNetworkCookieJar::UrlIterator::UrlIterator(const UrlIterator& other)
	:
	fCookieJar(other.fCookieJar),
	fIterator(NULL),
	fList(NULL),
	fLastList(NULL),
	fElement(NULL),
	fLastElement(NULL),
	fIndex(0),
	fLastIndex(0),
	fUrl(other.fUrl)
{
	_Initialize();
}


/**
 * @brief Construct a URL iterator for cookies applicable to \a url.
 *
 * @param cookieJar  The jar to iterate over.
 * @param url        The URL used to filter cookies by domain and path.
 */
BNetworkCookieJar::UrlIterator::UrlIterator(const BNetworkCookieJar* cookieJar,
	const BUrl& url)
	:
	fCookieJar(const_cast<BNetworkCookieJar*>(cookieJar)),
	fIterator(NULL),
	fList(NULL),
	fLastList(NULL),
	fElement(NULL),
	fLastElement(NULL),
	fIndex(0),
	fLastIndex(0),
	fUrl(url)
{
	_Initialize();
}


/**
 * @brief Destructor — releases read locks and deletes the private iterator.
 */
BNetworkCookieJar::UrlIterator::~UrlIterator()
{
	if (fList != NULL)
		fList->Unlock();
	if (fLastList != NULL)
		fLastList->Unlock();

	delete fIterator;
}


/**
 * @brief Return whether more URL-matching cookies remain.
 *
 * @return true if Next() will return a valid cookie pointer.
 */
bool
BNetworkCookieJar::UrlIterator::HasNext() const
{
	return fElement;
}


/**
 * @brief Advance and return the current URL-matching cookie.
 *
 * @return A const pointer to the current BNetworkCookie, or NULL at end.
 */
const BNetworkCookie*
BNetworkCookieJar::UrlIterator::Next()
{
	if (!fElement)
		return NULL;

	const BNetworkCookie* result = fElement;
	_FindNext();
	return result;
}


/**
 * @brief Remove the last URL-matching cookie returned by Next().
 *
 * Removes the cookie from its per-domain list. If the list becomes empty,
 * it is also removed from the hash map.
 *
 * @return A const pointer to the removed cookie (caller takes ownership), or
 *         NULL if no cookie has been returned yet.
 */
const BNetworkCookie*
BNetworkCookieJar::UrlIterator::Remove()
{
	if (!fLastElement)
		return NULL;

	const BNetworkCookie* result = fLastElement;

	if (fCookieJar->fCookieHashMap->Lock()) {
		fLastList->Unlock();
		if (fLastList->LockForWriting() == B_OK) {
			fLastList->RemoveItemAt(fLastIndex);

			if (fLastList->CountItems() == 0) {
				fCookieJar->fCookieHashMap->Remove(fIterator->fCookieMapIterator);
				delete fLastList;
				fLastList = NULL;
			} else {
				fLastList->Unlock();
				fLastList->LockForReading();
			}
		}
		fCookieJar->fCookieHashMap->Unlock();
	}

	fLastElement = NULL;
	return result;
}


/**
 * @brief Assignment operator — resets and repositions this URL iterator.
 *
 * @param other  The source UrlIterator to copy.
 * @return A reference to this iterator.
 */
BNetworkCookieJar::UrlIterator&
BNetworkCookieJar::UrlIterator::operator=(
	const BNetworkCookieJar::UrlIterator& other)
{
	if (this == &other)
		return *this;

	// Teardown
	if (fList)
		fList->Unlock();

	delete fIterator;

	// Init
	fCookieJar = other.fCookieJar;
	fIterator = NULL;
	fList = NULL;
	fLastList = NULL;
	fElement = NULL;
	fLastElement = NULL;
	fIndex = 0;
	fLastIndex = 0;
	fUrl = other.fUrl;

	_Initialize();

	return *this;
}


/**
 * @brief Initialise the iterator by resolving the starting domain.
 *
 * Derives the search domain from fUrl, prepends a dot, and advances to the
 * first matching cookie via _FindNext().
 */
void
BNetworkCookieJar::UrlIterator::_Initialize()
{
	BString domain = fUrl.Host();

	if (!domain.Length()) {
		if (fUrl.Protocol() == "file")
			domain = "localhost";
		else
			return;
	}

	fIterator = new(std::nothrow) PrivateIterator(
		fCookieJar->fCookieHashMap->GetIterator());

	if (fIterator != NULL) {
		// Prepending a dot since _FindNext is going to call _SupDomain()
		domain.Prepend(".");
		fIterator->fKey.SetTo(domain, domain.Length());
		_FindNext();
	}
}


/**
 * @brief Move the iterator key to the next superdomain.
 *
 * Strips the leading component of the domain key (up to the first dot),
 * making the key one level higher in the domain hierarchy.
 *
 * @return true if a further superdomain exists, false if at the root.
 */
bool
BNetworkCookieJar::UrlIterator::_SuperDomain()
{
	BString domain(fIterator->fKey.GetString());
		// Makes a copy of the characters from the key. This is important,
		// because HashString doesn't like SetTo to be called with a substring
		// of its original string (use-after-free + memcpy overwrite).
	int32 firstDot = domain.FindFirst('.');
	if (firstDot < 0)
		return false;

	const char* nextDot = domain.String() + firstDot;

	fIterator->fKey.SetTo(nextDot + 1);
	return true;
}


/**
 * @brief Find the next URL-matching cookie, walking up the domain hierarchy.
 *
 * Calls _FindPath() on the current domain list, advancing to the next
 * superdomain via _SuperDomain() and _FindDomain() when the current list is
 * exhausted or contains no path-matching cookies.
 */
void
BNetworkCookieJar::UrlIterator::_FindNext()
{
	fLastIndex = fIndex;
	fLastElement = fElement;
	if (fLastList != NULL)
		fLastList->Unlock();

	fLastList = fList;
	if (fCookieJar->fCookieHashMap->Lock()) {
		if (fLastList)
			fLastList->LockForReading();

		while (!_FindPath()) {
			if (!_SuperDomain()) {
				fElement = NULL;
				fCookieJar->fCookieHashMap->Unlock();
				return;
			}

			_FindDomain();
		}
		fCookieJar->fCookieHashMap->Unlock();
	}
}


/**
 * @brief Load the cookie list for the current domain key into fList.
 *
 * Looks up the current fIterator->fKey in the hash map and, if found,
 * acquires a read lock on the resulting list.
 */
void
BNetworkCookieJar::UrlIterator::_FindDomain()
{
	if (fList != NULL)
		fList->Unlock();

	if (fCookieJar->fCookieHashMap->Lock()) {
		fList = fCookieJar->fCookieHashMap->Get(fIterator->fKey);

		if (fList == NULL)
			fElement = NULL;
		else {
			fList->LockForReading();
		}
		fCookieJar->fCookieHashMap->Unlock();
	}

	fIndex = -1;
}


/**
 * @brief Find the next cookie in the current list that matches the URL path.
 *
 * Advances fIndex through fList and calls IsValidForPath() on each cookie
 * until a match is found or the list is exhausted.
 *
 * @return true if a matching cookie was found and fElement is set, false if
 *         no matching cookie remains in the current domain list.
 */
bool
BNetworkCookieJar::UrlIterator::_FindPath()
{
	fIndex++;
	while (fList && fIndex < fList->CountItems()) {
		fElement = fList->ItemAt(fIndex);

		if (fElement->IsValidForPath(fUrl.Path()))
			return true;

		fIndex++;
	}

	return false;
}


// #pragma mark - BNetworkCookieList


/**
 * @brief Construct a BNetworkCookieList and initialise the reader-writer lock.
 */
BNetworkCookieList::BNetworkCookieList()
{
	pthread_rwlock_init(&fLock, NULL);
}


/**
 * @brief Destructor — destroys the reader-writer lock.
 *
 * Expected to be called with the write lock held by the cookie jar destructor.
 */
BNetworkCookieList::~BNetworkCookieList()
{
	// Note: this is expected to be called with the write lock held.
	pthread_rwlock_destroy(&fLock);
}


/**
 * @brief Acquire a shared read lock on the list.
 *
 * @return B_OK on success, or an error code if locking fails.
 */
status_t
BNetworkCookieList::LockForReading()
{
	return pthread_rwlock_rdlock(&fLock);
}


/**
 * @brief Acquire an exclusive write lock on the list.
 *
 * @return B_OK on success, or an error code if locking fails.
 */
status_t
BNetworkCookieList::LockForWriting()
{
	return pthread_rwlock_wrlock(&fLock);
}


/**
 * @brief Release any held lock (read or write) on the list.
 *
 * @return B_OK on success, or an error code if unlocking fails.
 */
status_t
BNetworkCookieList::Unlock()
{
	return pthread_rwlock_unlock(&fLock);
}
