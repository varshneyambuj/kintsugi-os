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
 *   Copyright 2009, Adrien Destugues, pulkomandy@gmail.com. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file HashMapCatalog.cpp
 * @brief Abstract hash-map based catalog backend for the Locale Kit.
 *
 * HashMapCatalog provides the in-memory storage layer for catalog add-ons.
 * String keys are hashed from the (string, context, comment) triple using a
 * polynomial rolling hash. The fingerprint is a sum of all key hashes, making
 * it order-independent so catalogs for different languages of the same app
 * match each other's fingerprint. Concrete subclasses (DefaultCatalog,
 * PlainTextCatalog) add serialization on top of this layer.
 *
 * @see DefaultCatalog, BCatalogData
 */


#include <HashMapCatalog.h>

#include <ByteOrder.h>

#include <stdlib.h>


namespace BPrivate {


/*
 * This is the standard implementation of a localization catalog, using a hash
 * map. This class is abstract, you need to inherit it and provide methodes for
 * reading and writing the catalog to a file. Classes doing that are
 * HashMapCatalog and PlainTextCatalog.
 * If you ever need to create a catalog not built around an hash map, inherit
 * BCatalogData instead. Note that in this case you will not be able to use our
 * development tools anymore.
 */


/**
 * @brief Construct a CatKey from three string components.
 *
 * Computes a combined hash from the string, context, and comment so that
 * lookups can short-circuit on hash mismatch.
 *
 * @param str  Original source string.
 * @param ctx  Disambiguation context string.
 * @param cmt  Translator comment string.
 */
CatKey::CatKey(const char *str, const char *ctx, const char *cmt)
	:
	fString(str),
	fContext(ctx),
	fComment(cmt),
	fFlags(0)
{
	fHashVal = HashFun(fString.String(),0);
	fHashVal = HashFun(fContext.String(),fHashVal);
	fHashVal = HashFun(fComment.String(),fHashVal);
}


/**
 * @brief Construct a CatKey from a pre-computed numeric hash identifier.
 *
 * Used for numeric-ID based catalog entries where no string parts exist.
 *
 * @param id  Pre-computed 32-bit hash value.
 */
CatKey::CatKey(uint32 id)
	:
	fHashVal(id),
	fFlags(0)
{
}


/**
 * @brief Construct an empty, zero-initialized CatKey.
 */
CatKey::CatKey()
	:
	fHashVal(0),
	fFlags(0)
{
}


/**
 * @brief Test two CatKey objects for equality.
 *
 * Both the hash value and all three string components must match to guard
 * against hash collisions.
 *
 * @param right  The other CatKey to compare against.
 * @return true if hash and all string fields are equal.
 */
bool
CatKey::operator== (const CatKey& right) const
{
	// Two keys are equal if their hashval and key (string,context,comment)
	// are equal (testing only the hash would not filter out collisions):
	return fHashVal == right.fHashVal
		&& fString == right.fString
		&& fContext == right.fContext
		&& fComment == right.fComment;
}


/**
 * @brief Test two CatKey objects for inequality.
 *
 * @param right  The other CatKey to compare against.
 * @return true if any field differs.
 */
bool
CatKey::operator!= (const CatKey& right) const
{
	// Two keys are equal if their hashval and key (string,context,comment)
	// are equal (testing only the hash would not filter out collisions):
	return fHashVal != right.fHashVal
		|| fString != right.fString
		|| fContext != right.fContext
		|| fComment != right.fComment;
}


/**
 * @brief Extract the three string components of this key into output pointers.
 *
 * Any of the output pointers may be NULL if the corresponding component is
 * not needed.
 *
 * @param str  Output BString for the source string, or NULL.
 * @param ctx  Output BString for the context, or NULL.
 * @param cmt  Output BString for the comment, or NULL.
 * @return B_OK always.
 */
status_t
CatKey::GetStringParts(BString* str, BString* ctx, BString* cmt) const
{
	if (str) *str = fString;
	if (ctx) *ctx = fContext;
	if (cmt) *cmt = fComment;

	return B_OK;
}


/**
 * @brief Compute a polynomial rolling hash over a C string.
 *
 * An extra step is appended after the last character to differentiate
 * concatenations such as ("ab","cd","ef") from ("abcd","e","f").
 *
 * @param s           Null-terminated input string.
 * @param startValue  Initial hash value (chain from a previous call to combine).
 * @return The 32-bit hash value.
 */
uint32
CatKey::HashFun(const char* s, int startValue) {
	unsigned long h = startValue;
	for ( ; *s; ++s)
		h = 5 * h + *s;

	// Add 1 to differenciate ("ab","cd","ef") from ("abcd","e","f")
	h = 5 * h + 1;

	return size_t(h);
}


// HashMapCatalog


/**
 * @brief Remove all entries from the catalog hash map.
 */
void
HashMapCatalog::MakeEmpty()
{
	fCatMap.Clear();
}


/**
 * @brief Return the number of entries currently stored in the catalog.
 *
 * @return Entry count as a 32-bit integer.
 */
int32
HashMapCatalog::CountItems() const
{
	return fCatMap.Size();
}


/**
 * @brief Look up a translated string by original text, context, and comment.
 *
 * @param string   Original source string.
 * @param context  Disambiguation context, or NULL.
 * @param comment  Translator comment, or NULL.
 * @return The translated string, or NULL if not found.
 */
const char *
HashMapCatalog::GetString(const char *string, const char *context,
	const char *comment)
{
	CatKey key(string, context, comment);
	return GetString(key);
}


/**
 * @brief Look up a translated string by numeric identifier.
 *
 * @param id  Numeric catalog key (pre-computed hash).
 * @return The translated string, or NULL if not found.
 */
const char *
HashMapCatalog::GetString(uint32 id)
{
	CatKey key(id);
	return GetString(key);
}


/**
 * @brief Look up a translated string by CatKey.
 *
 * @param key  The fully constructed CatKey to look up.
 * @return The translated string, or NULL if not found.
 */
const char *
HashMapCatalog::GetString(const CatKey& key)
{
	BString value = fCatMap.Get(key);
	if (value.Length() == 0)
		return NULL;
	else
		return value.String();
}


/**
 * @brief Expand C escape sequences in a BString in-place.
 *
 * Handles standard escapes (\\n, \\t, \\r, etc.) and two-digit hex sequences
 * (\\xHH). Unknown escape sequences have their backslash dropped.
 *
 * @param stringToParse  The BString to process in-place.
 * @return B_OK on success, B_ERROR if the buffer lock fails.
 */
static status_t
parseQuotedChars(BString& stringToParse)
{
	char* in = stringToParse.LockBuffer(0);
	if (in == NULL)
		return B_ERROR;
	char* out = in;
	int newLength = 0;
	bool quoted = false;

	while (*in != 0) {
		if (quoted) {
			if (*in == 'a')
				*out = '\a';
			else if (*in == 'b')
				*out = '\b';
			else if (*in == 'f')
				*out = '\f';
			else if (*in == 'n')
				*out = '\n';
			else if (*in == 'r')
				*out = '\r';
			else if (*in == 't')
				*out = '\t';
			else if (*in == 'v')
				*out = '\v';
			else if (*in == '"')
				*out = '"';
			else if (*in == 'x') {
				if (in[1] == '\0' || in[2] == '\0')
					break;
				// Parse the 2-digit hex integer that follows
				char tmp[3];
				tmp[0] = in[1];
				tmp[1] = in[2];
				tmp[2] = '\0';
				unsigned int hexchar = strtoul(tmp, NULL, 16);
				*out = hexchar;
				// skip the number
				in += 2;
			} else {
				// drop quote from unknown quoting-sequence:
				*out = *in ;
			}
			quoted = false;
			out++;
			newLength++;
		} else {
			quoted = (*in == '\\');
			if (!quoted) {
				*out = *in;
				out++;
				newLength++;
			}
		}
		in++;
	}
	*out = '\0';
	stringToParse.UnlockBuffer(newLength);

	return B_OK;
}


/**
 * @brief Store a translated string keyed by original text, context, and comment.
 *
 * Parses C escape sequences in \a string, \a translated, and \a comment before
 * storing. Overwrites any existing entry with the same key.
 *
 * @param string      Original source string (escape sequences will be expanded).
 * @param translated  Translated string (escape sequences will be expanded).
 * @param context     Disambiguation context string.
 * @param comment     Translator comment (escape sequences will be expanded).
 * @return B_OK on success, or an error code if escape parsing fails.
 */
status_t
HashMapCatalog::SetString(const char *string, const char *translated,
	const char *context, const char *comment)
{
	BString stringCopy(string);
	status_t result = parseQuotedChars(stringCopy);
	if (result != B_OK)
		return result;

	BString translatedCopy(translated);
	if ((result = parseQuotedChars(translatedCopy)) != B_OK)
		return result;

	BString commentCopy(comment);
	if ((result = parseQuotedChars(commentCopy)) != B_OK)
		return result;

	CatKey key(stringCopy.String(), context, commentCopy.String());
	return fCatMap.Put(key, translatedCopy.String());
		// overwrite existing element
}


/**
 * @brief Store a translated string keyed by numeric identifier.
 *
 * Parses C escape sequences in \a translated before storing.
 *
 * @param id          Numeric catalog key.
 * @param translated  Translated string (escape sequences will be expanded).
 * @return B_OK on success, or an error code if escape parsing fails.
 */
status_t
HashMapCatalog::SetString(int32 id, const char *translated)
{
	BString translatedCopy(translated);
	status_t result = parseQuotedChars(translatedCopy);
	if (result != B_OK)
		return result;
	CatKey key(id);
	return fCatMap.Put(key, translatedCopy.String());
		// overwrite existing element
}


/**
 * @brief Store a translated string using a pre-constructed CatKey.
 *
 * Parses C escape sequences in \a translated before storing.
 *
 * @param key         Pre-built CatKey identifying the source string.
 * @param translated  Translated string (escape sequences will be expanded).
 * @return B_OK on success, or an error code if escape parsing fails.
 */
status_t
HashMapCatalog::SetString(const CatKey& key, const char *translated)
{
	BString translatedCopy(translated);
	status_t result = parseQuotedChars(translatedCopy);
	if (result != B_OK)
		return result;
	return fCatMap.Put(key, translatedCopy.String());
		// overwrite existing element
}


/*
 * computes a checksum (we call it fingerprint) on all the catalog-keys. We do
 * not include the values, since we want catalogs for different languages of the
 * same app to have the same fingerprint, since we use it to separate different
 * catalog-versions. We use a simple sum because there is no well known
 * checksum algorithm that gives the same result if the string are sorted in the
 * wrong order, and this does happen, as an hash map is an unsorted container.
 */
/**
 * @brief Compute and return the catalog fingerprint over all key hashes.
 *
 * The fingerprint is a little-endian sum of all entry key hash values so that
 * catalogs for different languages of the same app share the same fingerprint.
 *
 * @return The 32-bit fingerprint checksum.
 */
uint32
HashMapCatalog::ComputeFingerprint() const
{
	uint32 checksum = 0;

	int32 hash;
	CatMap::Iterator iter = fCatMap.GetIterator();
	CatMap::Entry entry;
	while (iter.HasNext()) {
		entry = iter.Next();
		hash = B_HOST_TO_LENDIAN_INT32(entry.key.fHashVal);
		checksum += hash;
	}
	return checksum;
}


/**
 * @brief Recompute the fingerprint and store it in fFingerprint.
 */
void
HashMapCatalog::UpdateFingerprint()
{
	fFingerprint = ComputeFingerprint();
}


}	// namespace BPrivate
