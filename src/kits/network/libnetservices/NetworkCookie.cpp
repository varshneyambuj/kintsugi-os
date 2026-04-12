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
 *       Adrien Destugues, pulkomandy@pulkomandy.tk
 *       Christophe Huriaux, c.huriaux@gmail.com
 *       Hamish Morrison, hamishm53@gmail.com
 */


/**
 * @file NetworkCookie.cpp
 * @brief Implementation of BNetworkCookie for RFC 6265 HTTP cookie handling.
 *
 * Parses Set-Cookie header values, validates domain and path restrictions,
 * enforces host-only and secure flags, supports archiving via BMessage, and
 * provides lazy-cached raw cookie string generation. Cookie expiry is handled
 * via max-age and expires attributes following the RFC 6265 algorithm.
 *
 * @see BNetworkCookieJar, BHttpRequest
 */


#include <new>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <Debug.h>
#include <HttpTime.h>
#include <NetworkCookie.h>

using namespace BPrivate::Network;


static const char* kArchivedCookieName = "be:cookie.name";
static const char* kArchivedCookieValue = "be:cookie.value";
static const char* kArchivedCookieDomain = "be:cookie.domain";
static const char* kArchivedCookiePath = "be:cookie.path";
static const char* kArchivedCookieExpirationDate = "be:cookie.expirationdate";
static const char* kArchivedCookieSecure = "be:cookie.secure";
static const char* kArchivedCookieHttpOnly = "be:cookie.httponly";
static const char* kArchivedCookieHostOnly = "be:cookie.hostonly";


/**
 * @brief Construct a cookie from explicit name, value, and originating URL.
 *
 * Sets the domain from the URL host and derives the default path via
 * _DefaultPathForUrl(). For file:// URLs with no host, the domain is forced
 * to "localhost".
 *
 * @param name   The cookie name.
 * @param value  The cookie value.
 * @param url    The URL from which this cookie is being set.
 */
BNetworkCookie::BNetworkCookie(const char* name, const char* value,
	const BUrl& url)
{
	_Reset();
	fName = name;
	fValue = value;

	SetDomain(url.Host());

	if (url.Protocol() == "file" && url.Host().Length() == 0) {
		SetDomain("localhost");
			// make sure cookies set from a file:// URL are stored somewhere.
	}

	SetPath(_DefaultPathForUrl(url));
}


/**
 * @brief Construct a cookie by parsing a Set-Cookie header string.
 *
 * Delegates to ParseCookieString() after resetting all fields. If parsing
 * fails, fInitStatus reflects the error.
 *
 * @param cookieString  The Set-Cookie header value to parse.
 * @param url           The URL from which the Set-Cookie header was received.
 */
BNetworkCookie::BNetworkCookie(const BString& cookieString, const BUrl& url)
{
	_Reset();
	fInitStatus = ParseCookieString(cookieString, url);
}


/**
 * @brief Construct a cookie by restoring it from a BMessage archive.
 *
 * Reads name, value, domain, path, secure, HttpOnly, host-only, and
 * expiration date from the archive. Both string and legacy int32 expiration
 * formats are handled.
 *
 * @param archive  The BMessage archive previously produced by Archive().
 */
BNetworkCookie::BNetworkCookie(BMessage* archive)
{
	_Reset();

	archive->FindString(kArchivedCookieName, &fName);
	archive->FindString(kArchivedCookieValue, &fValue);

	archive->FindString(kArchivedCookieDomain, &fDomain);
	archive->FindString(kArchivedCookiePath, &fPath);
	archive->FindBool(kArchivedCookieSecure, &fSecure);
	archive->FindBool(kArchivedCookieHttpOnly, &fHttpOnly);
	archive->FindBool(kArchivedCookieHostOnly, &fHostOnly);

	// We store the expiration date as a string, which should not overflow.
	// But we still parse the old archive format, where an int32 was used.
	BString expirationString;
	int32 expiration;
	if (archive->FindString(kArchivedCookieExpirationDate, &expirationString)
			== B_OK) {
		BDateTime time = BHttpTime(expirationString).Parse();
		SetExpirationDate(time);
	} else if (archive->FindInt32(kArchivedCookieExpirationDate, &expiration)
			== B_OK) {
		SetExpirationDate((time_t)expiration);
	}
}


/**
 * @brief Default constructor — creates an uninitialised empty cookie.
 */
BNetworkCookie::BNetworkCookie()
{
	_Reset();
}


/**
 * @brief Destructor.
 */
BNetworkCookie::~BNetworkCookie()
{
}


// #pragma mark String to cookie fields


/**
 * @brief Parse a Set-Cookie header value and populate the cookie fields.
 *
 * Implements the RFC 6265 set-cookie-string parsing algorithm. Reads the
 * mandatory name=value pair first, then processes semicolon-separated
 * attribute-value pairs for Secure, HttpOnly, Max-Age, Expires, Domain, and
 * Path. Validates that the resulting cookie may legitimately be set from the
 * originating URL.
 *
 * @param string  The Set-Cookie header value string (without the field name).
 * @param url     The URL from which the header was received.
 * @return B_OK on success, B_BAD_DATA if the name=value pair is malformed,
 *         B_BAD_VALUE for invalid attribute values, or B_NOT_ALLOWED if the
 *         cookie domain does not match the request origin.
 */
status_t
BNetworkCookie::ParseCookieString(const BString& string, const BUrl& url)
{
	_Reset();

	// Set default values (these can be overriden later on)
	SetPath(_DefaultPathForUrl(url));
	SetDomain(url.Host());
	fHostOnly = true;
	if (url.Protocol() == "file" && url.Host().Length() == 0) {
		fDomain = "localhost";
			// make sure cookies set from a file:// URL are stored somewhere.
			// not going through SetDomain as it requires at least one '.'
			// in the domain (to avoid setting cookies on TLDs).
	}

	BString name;
	BString value;
	int32 index = 0;

	// Parse the name and value of the cookie
	index = _ExtractNameValuePair(string, name, value, index);
	if (index == -1 || value.Length() > 4096) {
		// The set-cookie-string is not valid
		return B_BAD_DATA;
	}

	SetName(name);
	SetValue(value);

	// Note on error handling: even if there are parse errors, we will continue
	// and try to parse as much from the cookie as we can.
	status_t result = B_OK;

	// Parse the remaining cookie attributes.
	while (index < string.Length()) {
		ASSERT(string[index] == ';');
		index++;

		index = _ExtractAttributeValuePair(string, name, value, index);

		if (name.ICompare("secure") == 0)
			SetSecure(true);
		else if (name.ICompare("httponly") == 0)
			SetHttpOnly(true);

		// The following attributes require a value.

		if (name.ICompare("max-age") == 0) {
			if (value.IsEmpty()) {
				result = B_BAD_VALUE;
				continue;
			}
			// Validate the max-age value.
			char* end = NULL;
			errno = 0;
			long maxAge = strtol(value.String(), &end, 10);
			if (*end == '\0')
				SetMaxAge((int)maxAge);
			else if (errno == ERANGE && maxAge == LONG_MAX)
				SetMaxAge(INT_MAX);
			else
				SetMaxAge(-1); // cookie will expire immediately
		} else if (name.ICompare("expires") == 0) {
			if (value.IsEmpty()) {
				// Will be a session cookie.
				continue;
			}
			BDateTime parsed = BHttpTime(value).Parse();
			SetExpirationDate(parsed);
		} else if (name.ICompare("domain") == 0) {
			if (value.IsEmpty()) {
				result = B_BAD_VALUE;
				continue;
			}

			status_t domainResult = SetDomain(value);
			// Do not reset the result to B_OK if something else already failed
			if (result == B_OK)
				result = domainResult;
		} else if (name.ICompare("path") == 0) {
			if (value.IsEmpty()) {
				result = B_BAD_VALUE;
				continue;
			}
			status_t pathResult = SetPath(value);
			if (result == B_OK)
				result = pathResult;
		}
	}

	if (!_CanBeSetFromUrl(url))
		result = B_NOT_ALLOWED;

	if (result != B_OK)
		_Reset();

	return result;
}


// #pragma mark Cookie fields modification


/**
 * @brief Set the cookie name and invalidate the raw cookie cache.
 *
 * @param name  The new cookie name.
 * @return A reference to this cookie for method chaining.
 */
BNetworkCookie&
BNetworkCookie::SetName(const BString& name)
{
	fName = name;
	fRawFullCookieValid = false;
	fRawCookieValid = false;
	return *this;
}


/**
 * @brief Set the cookie value and invalidate the raw cookie cache.
 *
 * @param value  The new cookie value.
 * @return A reference to this cookie for method chaining.
 */
BNetworkCookie&
BNetworkCookie::SetValue(const BString& value)
{
	fValue = value;
	fRawFullCookieValid = false;
	fRawCookieValid = false;
	return *this;
}


/**
 * @brief Set the cookie path attribute.
 *
 * Validates that the path begins with '/', does not exceed 4096 characters,
 * and contains no "." or ".." path segments.
 *
 * @param to  The path string to set.
 * @return B_OK on success, or B_BAD_DATA if validation fails.
 */
status_t
BNetworkCookie::SetPath(const BString& to)
{
	fPath.Truncate(0);
	fRawFullCookieValid = false;

	// Limit the path to 4096 characters to not let the cookie jar grow huge.
	if (to[0] != '/' || to.Length() > 4096)
		return B_BAD_DATA;

	// Check that there aren't any "." or ".." segments in the path.
	if (to.EndsWith("/.") || to.EndsWith("/.."))
		return B_BAD_DATA;
	if (to.FindFirst("/../") >= 0 || to.FindFirst("/./") >= 0)
		return B_BAD_DATA;

	fPath = to;
	return B_OK;
}


/**
 * @brief Set the cookie domain attribute.
 *
 * Strips a leading dot (RFC 2109 compatibility), validates that the domain
 * contains at least one interior dot (preventing cookies on TLDs), converts
 * to lowercase, and clears the host-only flag.
 *
 * @param domain  The domain string to set.
 * @return B_OK on success, or B_BAD_DATA if the domain is invalid.
 */
status_t
BNetworkCookie::SetDomain(const BString& domain)
{
	// TODO: canonicalize the domain
	BString newDomain = domain;

	// RFC 2109 (legacy) support: domain string may start with a dot,
	// meant to indicate the cookie should also be used for subdomains.
	// RFC 6265 makes all cookies work for subdomains, unless the domain is
	// not specified at all (in this case it has to exactly match the Url of
	// the page that set the cookie). In any case, we don't need to handle
	// dot-cookies specifically anymore, so just remove the extra dot.
	if (newDomain[0] == '.')
		newDomain.Remove(0, 1);

	// check we're not trying to set a cookie on a TLD or empty domain
	if (newDomain.FindLast('.') <= 0)
		return B_BAD_DATA;

	fDomain = newDomain.ToLower();

	fHostOnly = false;

	fRawFullCookieValid = false;
	return B_OK;
}


/**
 * @brief Set the cookie expiration to now plus \a maxAge seconds.
 *
 * Saturates at INT_MAX to avoid overflow for very large max-age values.
 * Negative values cause immediate expiry.
 *
 * @param maxAge  Number of seconds from now until the cookie expires.
 * @return A reference to this cookie for method chaining.
 */
BNetworkCookie&
BNetworkCookie::SetMaxAge(int32 maxAge)
{
	BDateTime expiration = BDateTime::CurrentDateTime(B_LOCAL_TIME);

	// Compute the expiration date (watch out for overflows)
	int64_t date = expiration.Time_t();
	date += (int64_t)maxAge;
	if (date > INT_MAX)
		date = INT_MAX;

	expiration.SetTime_t(date);

	return SetExpirationDate(expiration);
}


/**
 * @brief Set the cookie expiration date from a POSIX time_t value.
 *
 * @param expireDate  POSIX timestamp of the expiration date.
 * @return A reference to this cookie for method chaining.
 */
BNetworkCookie&
BNetworkCookie::SetExpirationDate(time_t expireDate)
{
	BDateTime expiration;
	expiration.SetTime_t(expireDate);
	return SetExpirationDate(expiration);
}


/**
 * @brief Set the cookie expiration date from a BDateTime value.
 *
 * If the date is invalid, the cookie becomes a session cookie (no expiry).
 *
 * @param expireDate  The BDateTime expiration value.
 * @return A reference to this cookie for method chaining.
 */
BNetworkCookie&
BNetworkCookie::SetExpirationDate(BDateTime& expireDate)
{
	if (!expireDate.IsValid()) {
		fExpiration.SetTime_t(0);
		fSessionCookie = true;
	} else {
		fExpiration = expireDate;
		fSessionCookie = false;
	}

	fExpirationStringValid = false;
	fRawFullCookieValid = false;

	return *this;
}


/**
 * @brief Set the Secure attribute, restricting the cookie to HTTPS.
 *
 * @param secure  true to require HTTPS for this cookie, false otherwise.
 * @return A reference to this cookie for method chaining.
 */
BNetworkCookie&
BNetworkCookie::SetSecure(bool secure)
{
	fSecure = secure;
	fRawFullCookieValid = false;
	return *this;
}


/**
 * @brief Set the HttpOnly attribute, hiding the cookie from JavaScript.
 *
 * @param httpOnly  true to mark the cookie as HttpOnly, false otherwise.
 * @return A reference to this cookie for method chaining.
 */
BNetworkCookie&
BNetworkCookie::SetHttpOnly(bool httpOnly)
{
	fHttpOnly = httpOnly;
	fRawFullCookieValid = false;
	return *this;
}


// #pragma mark Cookie fields access


/**
 * @brief Return the cookie name.
 *
 * @return A const reference to the name BString.
 */
const BString&
BNetworkCookie::Name() const
{
	return fName;
}


/**
 * @brief Return the cookie value.
 *
 * @return A const reference to the value BString.
 */
const BString&
BNetworkCookie::Value() const
{
	return fValue;
}


/**
 * @brief Return the cookie domain.
 *
 * @return A const reference to the domain BString.
 */
const BString&
BNetworkCookie::Domain() const
{
	return fDomain;
}


/**
 * @brief Return the cookie path.
 *
 * @return A const reference to the path BString.
 */
const BString&
BNetworkCookie::Path() const
{
	return fPath;
}


/**
 * @brief Return the cookie expiration date as a POSIX time_t.
 *
 * @return The expiration timestamp, or 0 for session cookies.
 */
time_t
BNetworkCookie::ExpirationDate() const
{
	return fExpiration.Time_t();
}


/**
 * @brief Return the cookie expiration date as a formatted HTTP date string.
 *
 * Lazily generates and caches the string in the B_HTTP_TIME_FORMAT_COOKIE
 * format on first call.
 *
 * @return A const reference to the formatted expiration date BString.
 */
const BString&
BNetworkCookie::ExpirationString() const
{
	BHttpTime date(fExpiration);

	if (!fExpirationStringValid) {
		fExpirationString = date.ToString(B_HTTP_TIME_FORMAT_COOKIE);
		fExpirationStringValid = true;
	}

	return fExpirationString;
}


/**
 * @brief Return whether the Secure attribute is set.
 *
 * @return true if the cookie must only be sent over HTTPS.
 */
bool
BNetworkCookie::Secure() const
{
	return fSecure;
}


/**
 * @brief Return whether the HttpOnly attribute is set.
 *
 * @return true if the cookie should not be accessible from JavaScript.
 */
bool
BNetworkCookie::HttpOnly() const
{
	return fHttpOnly;
}


/**
 * @brief Return the raw cookie string, optionally including all attributes.
 *
 * The short form ("name=value") is cached in fRawCookie and rebuilt only when
 * name or value changes. The full form adds Domain, Expires, Path, Secure,
 * and HttpOnly attributes and is cached in fRawFullCookie.
 *
 * @param full  true to include all cookie attributes, false for name=value only.
 * @return A const reference to the cached raw cookie BString.
 */
const BString&
BNetworkCookie::RawCookie(bool full) const
{
	if (!fRawCookieValid) {
		fRawCookie.Truncate(0);
		fRawCookieValid = true;

		fRawCookie << fName << "=" << fValue;
	}

	if (!full)
		return fRawCookie;

	if (!fRawFullCookieValid) {
		fRawFullCookie = fRawCookie;
		fRawFullCookieValid = true;

		if (HasDomain())
			fRawFullCookie << "; Domain=" << fDomain;
		if (HasExpirationDate())
			fRawFullCookie << "; Expires=" << ExpirationString();
		if (HasPath())
			fRawFullCookie << "; Path=" << fPath;
		if (Secure())
			fRawFullCookie << "; Secure";
		if (HttpOnly())
			fRawFullCookie << "; HttpOnly";

	}

	return fRawFullCookie;
}


// #pragma mark Cookie test


/**
 * @brief Return whether the cookie has the host-only flag set.
 *
 * Host-only cookies are only sent to the exact domain they were set from,
 * not to subdomains.
 *
 * @return true if the cookie is host-only.
 */
bool
BNetworkCookie::IsHostOnly() const
{
	return fHostOnly;
}


/**
 * @brief Return whether the cookie is a session cookie (no expiry date).
 *
 * @return true if the cookie should be deleted when the browser session ends.
 */
bool
BNetworkCookie::IsSessionCookie() const
{
	return fSessionCookie;
}


/**
 * @brief Return whether the cookie is fully valid and ready for use.
 *
 * A cookie is valid if it was successfully initialised, has a non-empty name,
 * and has a domain set.
 *
 * @return true if the cookie is valid, false otherwise.
 */
bool
BNetworkCookie::IsValid() const
{
	return fInitStatus == B_OK && HasName() && HasDomain();
}


/**
 * @brief Test whether this cookie should be sent for the given URL.
 *
 * Checks the Secure flag against the URL scheme, and validates both the
 * domain and path components. For file:// URLs, requires domain "localhost".
 *
 * @param url  The URL to test against.
 * @return true if the cookie is applicable to \a url, false otherwise.
 */
bool
BNetworkCookie::IsValidForUrl(const BUrl& url) const
{
	if (Secure() && url.Protocol() != "https")
		return false;

	if (url.Protocol() == "file")
		return Domain() == "localhost" && IsValidForPath(url.Path());

	return IsValidForDomain(url.Host()) && IsValidForPath(url.Path());
}


/**
 * @brief Test whether this cookie's domain matches the given domain string.
 *
 * Host-only cookies require an exact match. Other cookies allow a subdomain
 * match where the request domain ends with ".<cookieDomain>".
 *
 * @param domain  The request host domain to test.
 * @return true if the cookie's domain covers \a domain.
 */
bool
BNetworkCookie::IsValidForDomain(const BString& domain) const
{
	// TODO: canonicalize both domains
	const BString& cookieDomain = Domain();

	int32 difference = domain.Length() - cookieDomain.Length();
	// If the cookie domain is longer than the domain string it cannot
	// be valid.
	if (difference < 0)
		return false;

	// If the cookie is host-only the domains must match exactly.
	if (IsHostOnly())
		return domain == cookieDomain;

	// FIXME do not do substring matching on IP addresses. The RFCs disallow it.

	// Otherwise, the domains must match exactly, or the domain must have a dot
	// character just before the common suffix.
	const char* suffix = domain.String() + difference;
	return (strcmp(suffix, cookieDomain.String()) == 0 && (difference == 0
		|| domain[difference - 1] == '.'));
}


/**
 * @brief Test whether this cookie's path is a prefix of the given path.
 *
 * The request path is normalised by stripping the filename component (the
 * part after the last '/') before comparison.
 *
 * @param path  The request path to test.
 * @return true if the cookie's path is a prefix of the normalised request path.
 */
bool
BNetworkCookie::IsValidForPath(const BString& path) const
{
	const BString& cookiePath = Path();
	BString normalizedPath = path;
	int slashPos = normalizedPath.FindLast('/');
	if (slashPos != normalizedPath.Length() - 1)
		normalizedPath.Truncate(slashPos + 1);

	if (normalizedPath.Length() < cookiePath.Length())
		return false;

	// The cookie path must be a prefix of the path string
	return normalizedPath.Compare(cookiePath, cookiePath.Length()) == 0;
}


/**
 * @brief Verify that this cookie may legitimately be set from \a url.
 *
 * Checks both domain and path constraints from the originating URL's
 * perspective (i.e. prevents setting cookies for unrelated domains).
 *
 * @param url  The URL from which the Set-Cookie header was received.
 * @return true if the cookie may be set from this URL, false otherwise.
 */
bool
BNetworkCookie::_CanBeSetFromUrl(const BUrl& url) const
{
	if (url.Protocol() == "file")
		return Domain() == "localhost" && _CanBeSetFromPath(url.Path());

	return _CanBeSetFromDomain(url.Host()) && _CanBeSetFromPath(url.Path());
}


/**
 * @brief Test whether the cookie domain is compatible with the origin domain.
 *
 * The origin may set cookies for itself or for a suffix domain that is a
 * proper superdomain of the origin (subdomain setting is allowed).
 *
 * @param domain  The origin host domain.
 * @return true if setting this cookie from \a domain is permitted.
 */
bool
BNetworkCookie::_CanBeSetFromDomain(const BString& domain) const
{
	// TODO: canonicalize both domains
	const BString& cookieDomain = Domain();

	int32 difference = domain.Length() - cookieDomain.Length();
	if (difference < 0) {
		// Setting a cookie on a subdomain is allowed.
		const char* suffix = cookieDomain.String() + difference;
		return (strcmp(suffix, domain.String()) == 0 && (difference == 0
			|| cookieDomain[difference - 1] == '.'));
	}

	// If the cookie is host-only the domains must match exactly.
	if (IsHostOnly())
		return domain == cookieDomain;

	// FIXME prevent supercookies with a domain of ".com" or similar
	// This is NOT as straightforward as relying on the last dot in the domain.
	// Here's a list of TLD:
	// https://github.com/rsimoes/Mozilla-PublicSuffix/blob/master/effective_tld_names.dat

	// FIXME do not do substring matching on IP addresses. The RFCs disallow it.

	// Otherwise, the domains must match exactly, or the domain must have a dot
	// character just before the common suffix.
	const char* suffix = domain.String() + difference;
	return (strcmp(suffix, cookieDomain.String()) == 0 && (difference == 0
		|| domain[difference - 1] == '.'));
}


/**
 * @brief Test whether the cookie path is consistent with the origin path.
 *
 * Either the cookie path must be a prefix of the origin path, or the origin
 * path must be a prefix of the cookie path.
 *
 * @param path  The origin request path.
 * @return true if setting this cookie from \a path is permitted.
 */
bool
BNetworkCookie::_CanBeSetFromPath(const BString& path) const
{
	BString normalizedPath = path;
	int slashPos = normalizedPath.FindLast('/');
	normalizedPath.Truncate(slashPos);

	if (Path().Compare(normalizedPath, normalizedPath.Length()) == 0)
		return true;
	else if (normalizedPath.Compare(Path(), Path().Length()) == 0)
		return true;
	return false;
}


// #pragma mark Cookie fields existence tests


/**
 * @brief Return whether the cookie has a non-empty name.
 *
 * @return true if Name().Length() > 0.
 */
bool
BNetworkCookie::HasName() const
{
	return fName.Length() > 0;
}


/**
 * @brief Return whether the cookie has a non-empty value.
 *
 * @return true if Value().Length() > 0.
 */
bool
BNetworkCookie::HasValue() const
{
	return fValue.Length() > 0;
}


/**
 * @brief Return whether the cookie has a non-empty domain.
 *
 * @return true if Domain().Length() > 0.
 */
bool
BNetworkCookie::HasDomain() const
{
	return fDomain.Length() > 0;
}


/**
 * @brief Return whether the cookie has a non-empty path.
 *
 * @return true if Path().Length() > 0.
 */
bool
BNetworkCookie::HasPath() const
{
	return fPath.Length() > 0;
}


/**
 * @brief Return whether the cookie has an explicit expiration date.
 *
 * @return true if the cookie is not a session cookie.
 */
bool
BNetworkCookie::HasExpirationDate() const
{
	return !IsSessionCookie();
}


// #pragma mark Cookie delete test


/**
 * @brief Return whether the cookie should be deleted when the session ends.
 *
 * @return true for session cookies or cookies that have already expired.
 */
bool
BNetworkCookie::ShouldDeleteAtExit() const
{
	return IsSessionCookie() || ShouldDeleteNow();
}


/**
 * @brief Return whether the cookie's expiration date has passed.
 *
 * @return true if the cookie has an expiration date and it is in the past.
 */
bool
BNetworkCookie::ShouldDeleteNow() const
{
	if (HasExpirationDate())
		return (BDateTime::CurrentDateTime(B_GMT_TIME) > fExpiration);

	return false;
}


// #pragma mark BArchivable members


/**
 * @brief Archive the cookie into a BMessage.
 *
 * Stores mandatory fields (name, value) and all optional fields that are set.
 * The expiration date is stored as an HTTP date string.
 *
 * @param into  The BMessage to archive into.
 * @param deep  Passed to BArchivable::Archive() (no child objects to archive).
 * @return B_OK on success, or an error code if any AddString/AddBool fails.
 */
status_t
BNetworkCookie::Archive(BMessage* into, bool deep) const
{
	status_t error = BArchivable::Archive(into, deep);

	if (error != B_OK)
		return error;

	error = into->AddString(kArchivedCookieName, fName);
	if (error != B_OK)
		return error;

	error = into->AddString(kArchivedCookieValue, fValue);
	if (error != B_OK)
		return error;


	// We add optional fields only if they're defined
	if (HasDomain()) {
		error = into->AddString(kArchivedCookieDomain, fDomain);
		if (error != B_OK)
			return error;
	}

	if (HasExpirationDate()) {
		error = into->AddString(kArchivedCookieExpirationDate,
			BHttpTime(fExpiration).ToString());
		if (error != B_OK)
			return error;
	}

	if (HasPath()) {
		error = into->AddString(kArchivedCookiePath, fPath);
		if (error != B_OK)
			return error;
	}

	if (Secure()) {
		error = into->AddBool(kArchivedCookieSecure, fSecure);
		if (error != B_OK)
			return error;
	}

	if (HttpOnly()) {
		error = into->AddBool(kArchivedCookieHttpOnly, fHttpOnly);
		if (error != B_OK)
			return error;
	}

	if (IsHostOnly()) {
		error = into->AddBool(kArchivedCookieHostOnly, true);
		if (error != B_OK)
			return error;
	}

	return B_OK;
}


/**
 * @brief Instantiate a BNetworkCookie from a BMessage archive.
 *
 * @param archive  The archive previously produced by Archive().
 * @return A new heap-allocated BNetworkCookie, or NULL on allocation failure.
 */
/*static*/ BArchivable*
BNetworkCookie::Instantiate(BMessage* archive)
{
	if (archive->HasString(kArchivedCookieName)
		&& archive->HasString(kArchivedCookieValue))
		return new(std::nothrow) BNetworkCookie(archive);

	return NULL;
}


// #pragma mark Overloaded operators


/**
 * @brief Equality operator — two cookies are equal if name and value match.
 *
 * @param other  The cookie to compare against.
 * @return true if both name and value are equal, false otherwise.
 */
bool
BNetworkCookie::operator==(const BNetworkCookie& other)
{
	// Equality : name and values equals
	return fName == other.fName && fValue == other.fValue;
}


/**
 * @brief Inequality operator — the negation of operator==.
 *
 * @param other  The cookie to compare against.
 * @return true if the cookies differ in name or value, false if they are equal.
 */
bool
BNetworkCookie::operator!=(const BNetworkCookie& other)
{
	return !(*this == other);
}


/**
 * @brief Reset all cookie fields to their default (empty/false) state.
 *
 * Called by constructors and ParseCookieString() before (re-)populating
 * the cookie from external input.
 */
void
BNetworkCookie::_Reset()
{
	fInitStatus = false;

	fName.Truncate(0);
	fValue.Truncate(0);
	fDomain.Truncate(0);
	fPath.Truncate(0);
	fExpiration = BDateTime();
	fSecure = false;
	fHttpOnly = false;

	fSessionCookie = true;
	fHostOnly = true;

	fRawCookieValid = false;
	fRawFullCookieValid = false;
	fExpirationStringValid = false;
}


/**
 * @brief Advance \a index past any leading space or tab characters.
 *
 * @param string  The string to scan.
 * @param index   The starting position.
 * @return The index of the first non-whitespace character at or after \a index.
 */
int32
skip_whitespace_forward(const BString& string, int32 index)
{
	while (index < string.Length() && (string[index] == ' '
			|| string[index] == '\t'))
		index++;
	return index;
}


/**
 * @brief Move \a index backwards past any trailing space or tab characters.
 *
 * @param string  The string to scan.
 * @param index   The starting position (moves towards index 0).
 * @return The index of the last non-whitespace character at or before \a index,
 *         or -1 if the entire prefix is whitespace.
 */
int32
skip_whitespace_backward(const BString& string, int32 index)
{
	while (index >= 0 && (string[index] == ' ' || string[index] == '\t'))
		index--;
	return index;
}


/**
 * @brief Extract the first name=value pair from a cookie string.
 *
 * Finds the first '=' and first ';' to delimit the pair, strips surrounding
 * whitespace from both name and value, and copies them into the output strings.
 *
 * @param cookieString  The full Set-Cookie string.
 * @param name          Output BString for the cookie name.
 * @param value         Output BString for the cookie value.
 * @param index         Starting parse position (typically 0).
 * @return The position of the terminating ';' character, or the string length
 *         if there is none, or -1 if no '=' was found (parse failure).
 */
int32
BNetworkCookie::_ExtractNameValuePair(const BString& cookieString,
	BString& name, BString& value, int32 index)
{
	// Find our name-value-pair and the delimiter.
	int32 firstEquals = cookieString.FindFirst('=', index);
	int32 nameValueEnd = cookieString.FindFirst(';', index);

	// If the set-cookie-string lacks a semicolon, the name-value-pair
	// is the whole string.
	if (nameValueEnd == -1)
		nameValueEnd = cookieString.Length();

	// If the name-value-pair lacks an equals, the parse should fail.
	if (firstEquals == -1 || firstEquals > nameValueEnd)
		return -1;

	int32 first = skip_whitespace_forward(cookieString, index);
	int32 last = skip_whitespace_backward(cookieString, firstEquals - 1);

	// If we lack a name, fail to parse.
	if (first > last)
		return -1;

	cookieString.CopyInto(name, first, last - first + 1);

	first = skip_whitespace_forward(cookieString, firstEquals + 1);
	last = skip_whitespace_backward(cookieString, nameValueEnd - 1);
	if (first <= last)
		cookieString.CopyInto(value, first, last - first + 1);
	else
		value.SetTo("");

	return nameValueEnd;
}


/**
 * @brief Extract one attribute-value pair from the cookie attributes string.
 *
 * Finds the next ';' or end-of-string to delimit the attribute, then splits
 * on the first '=' within that range. Quotes around the value are stripped.
 *
 * @param cookieString  The full Set-Cookie string.
 * @param attribute     Output BString for the attribute name.
 * @param value         Output BString for the attribute value (may be empty).
 * @param index         Starting parse position (just past the leading ';').
 * @return The position of the terminating ';' character or the string length.
 */
int32
BNetworkCookie::_ExtractAttributeValuePair(const BString& cookieString,
	BString& attribute, BString& value, int32 index)
{
	// Find the end of our cookie-av.
	int32 cookieAVEnd = cookieString.FindFirst(';', index);

	// If the unparsed-attributes lacks a semicolon, then the cookie-av is the
	// whole string.
	if (cookieAVEnd == -1)
		cookieAVEnd = cookieString.Length();

	int32 attributeNameEnd = cookieString.FindFirst('=', index);
	// If the cookie-av has no equals, the attribute-name is the entire
	// cookie-av and the attribute-value is empty.
	if (attributeNameEnd == -1 || attributeNameEnd > cookieAVEnd)
		attributeNameEnd = cookieAVEnd;

	int32 first = skip_whitespace_forward(cookieString, index);
	int32 last = skip_whitespace_backward(cookieString, attributeNameEnd - 1);

	if (first <= last)
		cookieString.CopyInto(attribute, first, last - first + 1);
	else
		attribute.SetTo("");

	if (attributeNameEnd == cookieAVEnd) {
		value.SetTo("");
		return cookieAVEnd;
	}

	first = skip_whitespace_forward(cookieString, attributeNameEnd + 1);
	last = skip_whitespace_backward(cookieString, cookieAVEnd - 1);
	if (first <= last)
		cookieString.CopyInto(value, first, last - first + 1);
	else
		value.SetTo("");

	// values may (or may not) have quotes around them.
	if (value[0] == '"' && value[value.Length() - 1] == '"') {
		value.Remove(0, 1);
		value.Remove(value.Length() - 1, 1);
	}

	return cookieAVEnd;
}


/**
 * @brief Derive the default cookie path from the URL as per RFC 6265.
 *
 * The default path is the portion of the URL path up to and including the
 * last '/' before the filename component, or an empty string if the path
 * is empty or does not start with '/'.
 *
 * @param url  The originating URL.
 * @return The default path string, or an empty string for the root case.
 */
BString
BNetworkCookie::_DefaultPathForUrl(const BUrl& url)
{
	const BString& path = url.Path();
	if (path.IsEmpty() || path.ByteAt(0) != '/')
		return "";

	int32 index = path.FindLast('/');
	if (index == 0)
		return "";

	BString newPath = path;
	newPath.Truncate(index);
	return newPath;
}
